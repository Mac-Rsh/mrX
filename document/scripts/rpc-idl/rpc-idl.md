# RPC IDL 编译脚本

## 用途

`rpc-idl`（`scripts/plugins/rpc_idl/rpc_idl.py`）将 RPC `.idl` 接口描述文件编译为
C 头文件（`mr_rpc_idl.h`）和 C 源文件（`idl.c`），分别提供请求/响应类型与
`MR_RPC(service, method)` 入口宏，以及编解码器、分发调用和 `MR_RPC_EXPORT` 注册表。若启用
`MR_USE_MODULE_RPC_SELF_DESC`，还会同时生成供 Host
端动态读取的运行时二进制自描述（MRID）数据。业务接入流程见 [RPC 模块指南](../../modules/rpc/rpc.md)。

## 常用命令

```bash
# 使用内置预处理器编译测试 IDL 到默认路径
python ./scripts/cli.py rpc-idl -i ./test/modules/rpc/rpc.idl --no-cpp

# 指定自定义输出目录、源文件路径和附加包含目录
python ./scripts/cli.py rpc-idl -i app/protocol.idl -o build/generated/include/mrX/modules/rpc -c build/generated/idl.c -I app/include --no-cpp
```

## 参数说明

| 选项             | 默认值                                   | 说明                                                                 |
|------------------|------------------------------------------|----------------------------------------------------------------------|
| `-i`, `--input`  | 必填                                     | 输入 `.idl` 文件路径                                                 |
| `-o`, `--output` | `./include/mrX/modules/rpc/mr_rpc_idl.h` | 输出目录，或名为 `mr_rpc_idl.h` 的头文件路径                         |
| `-c`, `--source` | `./modules/rpc/idl.c`                    | 输出 C 源文件路径                                                    |
| `-I <目录>`      | 无                                       | 附加 `#include` 搜索目录，可重复指定                                 |
| `--no-cpp`       | 关闭                                     | 跳过外部 C 预处理器（`gcc`/`clang`/`cpp`），强制使用内置有限预处理器 |

使用 `-o` 时只允许更改输出目录，头文件名必须固定为 `mr_rpc_idl.h`（若传入不以 `.h`
结尾且非现有文件的路径，脚本会自动将其视为目录并在其中创建 `mr_rpc_idl.h`
）。输入文件、头文件和源文件必须是三个互不相同的路径。

### IDL 语法与代码生成约定

- **标量类型**：`u8`、`u16`、`u32`、`u64`、`i8`、`i16`、`i32`、`i64`、`bool`、`f32`、`f64`。
- **数组类型**：`T[N]` 为定长数组；`T<N>` 为最大容量 `N` 个元素的变长数组（`N > 0`
  ，生成 C 结构中含 `.size` 与 `.data[N]`）。变长数组元素 `T`
  当前仅支持定长类型（标量、enum、定长数组或纯定长 struct），不可嵌套变长类型；`bool`
  在编解码时会校验值必须为 `0` 或 `1`。
- **复合类型与包含**：`enum` 底层必须为整数类型；类型需先声明后使用，同一命名空间内
  `enum`/`struct`/`service` 不可重名。文件包含使用 C 预处理器形式
  `#include "common.idl"` 或 `#include <vendor/types.idl>`。

```idl
enum status: u8 { ok = 0, busy, fail };

struct point { x: i16; y: i16; };

service lab @1 {
    mix @0 (title: u8<16>, values: f32<32>) -> (count: u32, blob: u8<40>);
    ping @3 () -> ();
    log @4 (msg: u8<48>); // 无 -> (...) 表示单向 oneway 方法
};
```

- **ID 与槽位限制**：service ID 范围为 `0..65534`，method ID 范围为 `0..65535`，省略
  `@id` 时从当前作用域顺序递增分配。单个请求/响应最多允许 256 个变长槽位及首个变长槽位后的
  256 个移动槽位，类型表达式最多 31 层数组后缀，响应最大 packed 字节数不超过
  `0x7fffffff`。
- **变长编码与空响应**：变长数组线上长度前缀根据 `N` 自动选用小端 `u8`/`u16`/
  `u32`，后跟 `size * sizeof(T)` 字节有效数据（多字节标量保持目标机字节序）。若响应仅由变长数组组成且所有
  `.size == 0`，在 `STREAM` 会话下直接返回 `0`，不发送空长度前缀。

### 运行时二进制自描述（MRID）

启用 `MR_USE_MODULE_RPC_SELF_DESC` 后，`idl.c` 会内嵌由 semantic `Schema`
直接编码的只读描述 blob，并注册内部自描述方法供 Host 分片读取（请求参数为
`u32 offset`，每次返回最大 `MR_CFG_RPC_SELF_DESC_CHUNK_SIZE` 字节，读到长度 `0`
表示结束）。

Blob 采用 5 字节未压缩头 + 可选 zlib 压缩 body 的结构：

```text
+0  4 bytes  magic = "MRID"
+4  1 byte   info = (format_version << 4) | compression   // 当前版本 2: 0x20 未压缩, 0x21 zlib(level=9)
+5  ...      body 或 zlib(body)                          // 仅当压缩后严格更小时启用 zlib
```

Body 依次连续编码 `string table`、`enum table`、`struct table`、`service table`
：所有计数、索引、ID 与无符号枚举值均使用 unsigned LEB128（有符号枚举值先做 ZigZag
再编码），字符串采用 `length(uvar) + UTF-8`（无结尾 NUL）。字段类型按递归 bytecode
编码：标量 `0x00`..`0x0a`（对应 `u8`..`f64`）、`0x20 <enum_idx>`、
`0x21 <struct_idx>`、`0x24 <N> <elem_type>`（`T[N]`）、`0x25 <N> <elem_type>`（
`T<N>`）。

## 输出与排错

两个输出文件采用临时文件成对原子替换，任一写入失败会回滚恢复旧文件。成功时输出：

```text
Build: <...>/mr_rpc_idl.h
Build: <...>/idl.c
```

修改 `.idl` 后必须重新运行 `rpc-idl` 并重新编译 `idl.c`，否则方法注册表与结构体定义会失配。

| 常见现象            | 排查方式                                                           |
|---------------------|--------------------------------------------------------------------|
| `-o` 文件名被拒绝   | `-o` 只能传目录，或完整指定以 `mr_rpc_idl.h` 结尾的路径            |
| `unknown type`      | 将被引用的 `enum` / `struct` 移到使用它的字段之前                  |
| ID 冲突             | 为 `service` 或同服务下的 `method` 分配唯一 `@id`，或省略显式 ID   |
| 参数列表语法错误    | 检查 `(...)` 末尾是否误加了尾逗号                                  |
| 预处理包含失败      | 改用 `#include` 并检查 `-I` 目录；无 `gcc`/`clang` 时加 `--no-cpp` |
| 链接找不到 RPC 方法 | 确认新生成的 `idl.c` 已加入工程编译                                |
