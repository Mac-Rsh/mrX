# RPC IDL 编译脚本

rpc-idl 将 RPC .idl 描述编译为 C 头文件和 C 源文件。头文件提供请求/响应类型和 `MR_RPC(service, method)`
入口宏，源文件提供解码、调用、编码和 `MR_RPC_EXPORT` 注册。命令入口是
`scripts/plugins/rpc_idl/rpc_idl.py`；生成文件由 RPC 模块自动使用，不能手工编辑。

RPC 服务方法从 schema 编写到 handler 接入的完整示例见 [RPC 模块指南](../../modules/rpc/rpc.md)。本页集中说明 IDL
语法、生成命令和脚本参数。

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

固定数组长度和变长数组上限都必须大于零：

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
    name: u8<24>;
    pos: point;
    tags: u8[4];
    values: f32<32>;
    st: status;
};
~~~

规则：

- enum 的底层类型必须是整数类型；未写值的枚举项从 0 开始按前一项递增。
- 类型必须先声明后使用；enum、struct 和 service 名称在同一命名空间中不能重复。
- `T[N]` 是恰好 `N` 个元素的定长数组；`T<N>` 是最多 `N` 个元素的变长数组，`N` 始终表示元素个数而不是字节数。
- `T<N>` 在生成的 C 类型中通过 `.size` 保存当前元素个数，通过 `.data` 保存容量为 `N` 的元素数组；线上只传长度前缀和有效元素。
- 当前版本要求 `T<N>` 的元素 `T` 必须是定长类型，可使用标量、enum、定长数组或只包含定长字段的 struct；变长数组不能直接或通过
  struct 再包含变长元素。
- 文本和原始字节都使用 `u8<N>` 表达；IDL 不区分字符编码，也不会额外保留或自动写入结尾 NUL。
- `u8[2][3]` 是固定长度的嵌套数组，所有数组都按声明顺序生成 C 数组。
- bool 解码和编码时会校验只能是 0 或 1。

### 服务和方法

服务可显式指定 0 到 65534 的协议 ID；方法 ID 可使用 0 到 65535。省略 ID 时从当前作用域的下一个 ID 顺序分配：

~~~idl
service lab @1 {
    mix @0 (
        title: u8<16>,
        count: u32,
        values: f32<32>,
        blob: u8<40>
    ) -> (
        title: u8<16>,
        count: u32,
        values: f32<32>,
        blob: u8<40>
    );

    ping @3 () -> ();

    log @4 (msg: u8<48>);
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

变长数组的最大元素个数由 `T<N>` 限制；解码会拒绝超长或截断负载，编码也会拒绝 `.size` 超过声明上限的数据。线上长度前缀根据
`N` 自动选择 `u8`、`u16` 或 `u32`，始终按小端编码；元素中的多字节标量仍使用目标机字节序，与 XLink 的 `byte_order`
对应。生成的解码器会在展开结束后将长度写回本机字节序的 `.size`，编码器则显式写出小端长度前缀。前缀后紧跟 `size * sizeof(T)`
字节的有效元素数据。

生成器在数组展开过程中检查每个请求/响应的槽位预算：最多 256 个变长槽位，以及首个变长槽位后的 256
个移动槽位；移动槽位指排在首个变长槽位之后、线上没有固定偏移、编解码时必须按变长字段实际长度
搬移的字段槽位；超限立即停止展开并报告诊断，避免在报错前把大数组完整展开再失败。单个类型表达式最多包含 31 层数组后缀，与
XLink 默认 MRID
类型深度限制一致。响应的最大 packed 大小不能超过 `0x7fffffff` 字节，因为 C 编码阶段通过有符号 `mr_err_t` 返回负载长度。

对于只由变长数组组成的响应，生成编码器会把“所有 `.size` 都为 0”视为语义空响应：当该会话正在生成 `STREAM` 时直接返回
0，不把仅由长度前缀构成的 wire payload 当作有效数据。普通 `RESPONSE` 不受此优化影响，空变长数组仍会正常编码长度前缀。

## 运行时二进制描述

`rpc-idl` 在完成预处理、语法解析、ID 分配和类型解析后，会直接从 semantic `Schema` 生成运行时自描述，而不是保存原始 `.idl`
文本或 AST。描述 blob 和完整的自描述 RPC call 都生成到 `idl.c`，并位于 `#if defined(MR_USE_MODULE_RPC_SELF_DESC)`
内；未启用该配置时，blob、RPC 方法代码和生成头中的 request/response 工作区都不会进入固件。

### 二进制封装

最终存入 ROM 的 blob 由一个固定 5 字节头和一个可压缩 body 组成：

```text
+0  4 bytes  magic = "MRID"
+4  1 byte   info
+5  ...      body 或 zlib(body)
```

`info` 的高 4 bit 是格式版本，低 4 bit 是压缩方式：

```text
info = (format_version << 4) | compression
```

当前格式版本为 `2`；压缩方式 `0` 表示不压缩，`1` 表示 zlib。因此当前未压缩文档的 info 为 `0x20`，zlib 文档为 `0x21`。
`MRID + info` 这 5 字节始终保持未压缩，Host 无需先解压即可识别格式和压缩算法。

body 按以下顺序连续编码，不保存 C 结构体 offset、alignment、`packed_size`、源码位置或生成符号名：

```text
string table
enum table
struct table
service table
```

所有计数、索引、service/method ID、定长数组长度、变长数组上限和无符号枚举值都使用 unsigned LEB128。string table 中的名称编码为
`length: uvar + UTF-8 bytes`，不保存结尾 NUL。名称先全局去重进入 string table，后续记录只引用 string index。出现顺序保持
semantic schema 的声明顺序，因此编码结果是确定的。

有符号枚举值先做 ZigZag，再按 unsigned LEB128 编码：

```text
 0 -> 0
-1 -> 1
 1 -> 2
-2 -> 3
```

### Body 记录布局

string table：

```text
string_count: uvar
repeat string_count times:
    byte_length: uvar
    utf8[byte_length]
```

enum table：

```text
enum_count: uvar
repeat enum_count times:
    name:       uvar string_index
    backing:    u8 scalar_type_code
    item_count: uvar
    repeat item_count times:
        name:   uvar string_index
        value:  uvar 或 ZigZag+uvar
```

struct table：

```text
struct_count: uvar
repeat struct_count times:
    name: uvar string_index
    fields
```

service table：

```text
service_count: uvar
repeat service_count times:
    name:         uvar string_index
    service_id:   uvar
    method_count: uvar
    repeat method_count times:
        name:       uvar string_index
        method_id:  uvar
        flags:      u8
        request_fields
        response_fields
```

`flags` 当前只使用 bit0：`1` 表示 oneway，`0` 表示请求-响应方法。field list 的公共格式为：

```text
field_count: uvar
repeat field_count times:
    name: uvar string_index
    type: type bytecode
```

### Type bytecode

字段类型按递归 bytecode 编码，因此嵌套数组不需要额外类型表：

| code   | 类型            | 后续数据                             |
|--------|-----------------|--------------------------------------|
| `0x00` | `u8`            | 无                                   |
| `0x01` | `u16`           | 无                                   |
| `0x02` | `u32`           | 无                                   |
| `0x03` | `u64`           | 无                                   |
| `0x04` | `i8`            | 无                                   |
| `0x05` | `i16`           | 无                                   |
| `0x06` | `i32`           | 无                                   |
| `0x07` | `i64`           | 无                                   |
| `0x08` | `bool`          | 无                                   |
| `0x09` | `f32`           | 无                                   |
| `0x0a` | `f64`           | 无                                   |
| `0x20` | enum            | `enum_index: uvar`                   |
| `0x21` | struct          | `struct_index: uvar`                 |
| `0x24` | 定长数组 `T[N]` | `N: uvar`，随后递归编码 element type |
| `0x25` | 变长数组 `T<N>` | `N: uvar`，随后递归编码 element type |

例如 `u8[4]` 编码为 `0x24, 0x04, 0x00`；`sample[2]` 编码为 `0x24, 0x02, 0x21, <sample_index>`；`f32<32>` 编码为
`0x25, 0x20, 0x09`。

### 压缩规则

生成器先完整编码未压缩 body，然后执行 `zlib.compress(body, level=9)`。只有当压缩结果严格小于原 body 时才使用 zlib，否则保存原
body：

```text
if sizeof(zlib(body)) < sizeof(body):
    blob = "MRID" + 0x21 + zlib(body)
else:
    blob = "MRID" + 0x20 + body
```

因此小型 schema 不会因为 zlib header/stream 开销而变大；板端也不需要任何压缩或解压代码，只把生成在 ROM 中的 blob
分片返回。解压工作完全由 Host 完成。

### 自描述读取

生成的 `idl.c` 会同时生成 decode/invoke/encode 并通过 `MR_RPC_EXPORT` 注册内部自描述方法。请求 payload 仅为一个
`u32 offset`，响应 payload 仅为该 offset 开始的 blob 分片，最大长度由 `MR_CFG_RPC_SELF_DESC_CHUNK_SIZE` 决定。Host 从
offset `0` 开始读取，每次执行 `offset += response.payload_size`；当 offset 等于 blob 长度时会收到成功的 0 字节响应，表示文档读取完成。

读取完整 blob 后，Host 应先校验前 4 字节 `MRID`，再解析 info；若 compression 为 zlib，则只对第 5 字节之后的数据执行 zlib
解压，得到上述 body 后再按顺序解析四个 table。读取协议和板端配置见 [RPC 模块指南](../../modules/rpc/rpc.md#rpc-自描述)。

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

rpc_idl.py 负责参数、路径和成对写入；compiler.py 负责预处理、解析、语义检查、布局计算和后端调度。frontend/ 生成 AST，semantic/
生成解析后的 `Schema`，backend/ 负责 C 代码与运行时二进制描述；其中 `backend/schema.py` 实现二进制编码、压缩和 `idl.c` blob
输出。
