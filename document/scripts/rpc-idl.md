# RPC IDL 编译脚本

rpc-idl 将 RPC .idl 描述编译为 C 头文件和 C 源文件。头文件提供请求/响应类型和 `MR_RPC(service, method)`
入口宏，源文件提供解码、调用、编码和 `MR_RPC_EXPORT` 注册。命令入口是
`scripts/plugins/rpc_idl/rpc_idl.py`；生成文件由 RPC 模块自动使用，不能手工编辑。

## 基本命令

从 mrX/ 目录执行：

~~~text
python ./scripts/cli.py rpc-idl -i ./test/modules/rpc/rpc.idl --no-cpp
~~~

默认生成：

~~~text
include/mrX/modules/rpc/mr_rpc_idl.h
modules/rpc/idl.c
~~~

两个文件会先写入临时文件，再成对替换；任一文件写入失败时会清理临时文件并尽量恢复 旧头文件，避免只更新一半。

## 参数和路径

| 选项         | 默认值                               | 说明                                       |
|--------------|--------------------------------------|--------------------------------------------|
| -i, --input  | 必填                                 | 输入 .idl 文件                             |
| -o, --output | include/mrX/modules/rpc/mr_rpc_idl.h | 输出目录，或名为 mr_rpc_idl.h 的头文件路径 |
| -c, --source | modules/rpc/idl.c                    | 输出 C 源文件路径                          |
| `-I <目录>`  | 无                                   | 额外包含搜索目录，可重复                   |
| --no-cpp     | 关闭                                 | 强制使用内置有限预处理器                   |

默认输出相对项目根目录解析；显式给出的相对输入、输出和 -I 路径相对当前会话目录解析。 最不容易出错的自定义写法是把 -o 写成目录：

~~~text
python ./scripts/cli.py rpc-idl -i app/protocol.idl -o build/generated/include/mrX/modules/rpc -c build/generated/idl.c -I app/include --no-cpp
~~~

如果 -o 指向文件，它的文件名必须严格是 mr_rpc_idl.h；指向不存在且带有 .h 以外后缀的
路径时，脚本会把它当成输出目录并在其中创建固定文件名。输入、头文件和源文件必须是 三个不同路径。

默认模式优先寻找 gcc、clang、cpp 运行外部 C 预处理器；找不到时回退到内置预处理器。 --no-cpp 适合没有 C 工具链或希望固定预处理行为的环境。

## IDL 语法

### 类型、枚举和结构体

支持的内置标量类型：

~~~text
u8  u16  u32  u64
i8  i16  i32  i64
bool  f32  f64
~~~

字符串和字节串必须给出大于零的最大长度；数组长度也必须大于零：

~~~idl
enum status: u8 {
    ok = 0,
    busy,
    fail,
};

struct point {
    x: i16;
    y: i16;
};

struct sample {
    id: u32;
    name: string<24>;
    pos: point;
    tags: u8[4];
    st: status;
};
~~~

规则：

- enum 的底层类型必须是整数类型；未写值的枚举项从 0 开始按前一项递增。
- 类型必须先声明后使用；enum、struct 和 service 名称在同一命名空间中不能重复。
- `string<N>` 在线上传输时带长度前缀，结构体中额外保留结尾 NUL；`bytes<N>` 不保留 NUL。
- u8[2][3] 是固定长度的嵌套数组，所有数组都按声明顺序生成 C 数组。
- bool 解码和编码时会校验只能是 0 或 1。

### 服务和方法

服务和方法可显式指定 0 到 65535 的协议 ID；省略时从当前作用域的下一个 ID 顺序分配：

~~~idl
service lab @1 {
    mix @0 (
        title: string<16>,
        count: u32,
        blob: bytes<40>
    ) -> (
        title: string<16>,
        count: u32,
        blob: bytes<40>
    );

    ping @3 () -> ();

    log @4 (msg: string<48>);
};
~~~

-> (... ) 表示请求-响应方法；没有箭头的是单向方法，只有请求没有响应。空的 ()
合法，但参数列表末尾不能有逗号。服务 ID 在所有服务中唯一，方法 ID 在同一服务中唯一； 显式 ID 冲突或超出范围会使编译失败。

### 包含文件

IDL 的包含使用 C 预处理器形式：

~~~idl
#include "common.idl"
#include <vendor/types.idl>
~~~

不要写语言级 include "..."；解析器会提示使用 #include。引号包含先查当前文件目录， 再查 -I 目录；尖括号包含查 -I
目录。包含文件中的声明会在编译前展开。

## 生成代码的接入

把生成的 modules/rpc/idl.c 纳入应用工程，并在业务代码中包含 RPC 头文件：

~~~c
#include <mrX/modules/rpc/mr_rpc.h>

MR_RPC(lab, ping) {
    return 0;
}
~~~

`MR_RPC(lab, ping)` 的参数名和指针类型来自生成头文件。带请求或响应的方法会获得相应的 request/response
指针；入口返回非负结果后，生成代码自动编码响应并由 RPC 模块发送。新增或修改 .idl 后，必须重新生成并重新编译
idl.c，否则方法注册表、类型和业务入口可能互不匹配。

变量长度字段的最大值由 string<N> / bytes<N> 限制；解码会拒绝超长或截断负载，编码也 会拒绝超过声明上限的数据。生成器还会拒绝超过内部
codec 槽位限制的过大数组或复杂布局。

## 输出和错误排查

成功输出通常包含两行：

~~~text
Build: <...>/mr_rpc_idl.h
Build: <...>/idl.c
~~~

| 现象                 | 处理方式                                                      |
|----------------------|---------------------------------------------------------------|
| 输入文件不存在       | 检查 -i，并确认当前会话目录                                   |
| -o 文件名被拒绝      | 只能指定目录，或使用固定文件名 mr_rpc_idl.h                   |
| unknown type         | 把 enum / struct 放到使用它的字段之前                         |
| ID 冲突              | 为服务或同一服务的方法重新分配唯一 @id，或删除多余的显式 ID   |
| 参数列表报尾逗号错误 | 删除 (...) 中最后一个逗号                                     |
| 预处理包含失败       | 检查 #include 写法和 -I 目录；没有 gcc/clang 时加 --no-cpp    |
| 应用链接不到方法     | 确认把新生成的 idl.c 加入工程，并重新编译所有使用生成头的代码 |

## 脚本关系

rpc_idl.py 负责参数、路径和成对写入；compiler.py 负责预处理、解析、语义检查、布局计算 和 C 代码生成。frontend/、semantic/、backend/
是内部库，不需要直接运行。
