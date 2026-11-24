# HTC 硬件描述编译脚本

## 用途

HTC（Hardware Tree Compiler，入口 `scripts/plugins/htc/htc.py`）将 DTS 风格的
`.hts` / `.htsi` 硬件描述文件编译为 C 头文件（默认 `include/mrX/mr_hardgen.h`），供
mrX 驱动按节点路径、标签（label）、别名（alias）和 `compatible` 索引板级硬件。

## 常用命令

在 `mrX/` 目录下编译板级描述文件：

```bash
python ./scripts/cli.py htc -i board.hts
```

指定附加包含目录、输出路径并强制使用内置预处理器：

```bash
python ./scripts/cli.py htc -i boards/demo.hts -I boards/include -o build/include/mr_hardgen.h --no-cpp
```

## 参数说明

| 选项             | 默认值                       | 说明                                           |
|------------------|------------------------------|------------------------------------------------|
| `-i`, `--input`  | 必填                         | 输入 `.hts` 或 `.htsi` 文件路径                |
| `-o`, `--output` | `./include/mrX/mr_hardgen.h` | 输出头文件路径，父目录不存在时自动创建         |
| `-I <目录>`      | 无                           | 附加包含搜索目录，可重复指定                   |
| `--no-cpp`       | 关闭                         | 跳过外部 C 预处理器，强制使用内置有限预处理器  |
| `--strict`       | 关闭                         | 对不属于标准 Linux DTS 的 HTC 扩展语法给出告警 |

默认模式下，HTC 按顺序查找系统中的 `gcc`、`clang`、`cpp`
作为预处理器，均未找到时自动回退到内置预处理器；若需保证不同主机环境预处理行为完全一致，可传入
`--no-cpp`。

### `.hts` 语法与扩展约定

```dts
/dts-v1/;

/ {
    compatible = "demo,board";

    soc {
        uart0: serial@40000000 {
            compatible = "demo,uart";
            reg = <0x40000000 0x1000>;
            status = "okay";
        };
    };

    aliases {
        serial0 = &uart0;
    };
};
```

常用语法如下：

| 写法                                 | 含义                                                                 |
|--------------------------------------|----------------------------------------------------------------------|
| `/dts-v1/;`                          | 入口文件版本声明（被包含文件中的重复声明会被忽略）                   |
| `label: name@4000 { ... };`          | 节点标签与十六进制 unit address（不带 `0x`）                         |
| `property;`                          | 标志属性，生成值为 `1` 的宏                                          |
| `property = "text";`                 | 字符串属性                                                           |
| `property = <1 2 3>;`                | 展开数组，元素支持数字、字符串和节点引用                             |
| `property = [00 11 ff];`             | DTS 字节串（两位十六进制字节），旧写法 `[1, 2, 3]` 亦兼容            |
| `property = &uart0;`                 | 节点引用（支持 `&label` 或 `&{/abs/path}`）                          |
| `&uart0 { ... };`                    | 按标签或 `&{/abs/path}` 覆写已有节点                                 |
| `/delete-node/ target;`              | 删除节点及其子树                                                     |
| `/delete-property/ name;`            | 删除当前节点中的属性                                                 |
| `#include "..."` / `/include/ "..."` | 包含其他 `.htsi` / `.hts` 文件（路径必须为相对路径，禁止 `..` 穿越） |

内置预处理器仅支持对象式 `#define`、`#undef`、`#if`、`#ifdef`、`#ifndef`、`#elif`、
`#else` 与 `#endif`。

`status` 属性缺省为 `"okay"`，除标准状态外还支持 mrX 的初始化顺序扩展：

| `status` 值                                 | 语义                                             |
|---------------------------------------------|--------------------------------------------------|
| `"early"` / `"okay"` / `"last"` / `"defer"` | 分别对应早期、普通、最后、延后初始化级别         |
| `"prio(0)"` ~ `"prio(9)"`                   | 指定数字优先级的初始化级别                       |
| `"disabled"`                                | 禁用该节点及其所有子节点，不生成节点宏和驱动实例 |

`--strict` 仅对 `early`/`last`/`defer`/`prio(n)`、裸数字、浮点数和裸标识符等 HTC
扩展语法输出兼容性告警，不加 `--strict` 时均视为合法语法。

## 输出与排错

编译成功后输出 `Build: <输出路径>`，头文件中包含三类宏定义：启用节点的属性宏（
`EXIST`、`NAME`、`PATH`、`INIT_LEVEL`、`PARENT`、`ADDR` 等）、`label`/`aliases` 映射，以及按
`compatible` 分组的实例数量和路径索引。驱动通过 `<mrX/drivers/mr_driver.h>`
间接引入该文件并使用 `MR_HT_*` 宏。修改 `.hts` 后必须重新运行 `htc`，不要手工编辑
`mr_hardgen.h`。

当输入文件不存在、预处理报错或语义校验失败时，命令退出码为 `1`
并打印带文件与行号的诊断信息，此时不会覆盖目标路径上原有的旧头文件。

| 常见现象               | 排查方式                                                                     |
|------------------------|------------------------------------------------------------------------------|
| `Input file not found` | 检查 `-i` 路径；显式相对路径相对当前会话目录解析                             |
| 包含文件找不到         | 确认路径为相对路径且不含 `..`，并通过 `-I` 添加搜索目录                      |
| 节点未进入驱动实例     | 检查节点是否包含字符串类型的 `compatible`，且自身与所有父节点均非 `disabled` |
| 引用解析失败           | 确认目标标签已在引用前声明，或改用正确的 `&{/absolute/path}`                 |
| 无 `gcc`/`clang` 环境  | 添加 `--no-cpp` 选项，并确保只使用内置预处理器支持的宏指令                   |
