# HTC 硬件描述编译脚本

HTC（Hardware Tree Compiler）把 DTS 风格的 .hts / .htsi 文件编译成 C 头文件，供 mrX
驱动通过节点路径、label、alias 和
compatible 查找板级硬件。命令入口是 scripts/plugins/htc/htc.py，编译流水线由同目录的
frontend、semantic、validation 和
backend 组成。

## 基本命令

从 mrX/ 目录执行：

~~~text
python ./scripts/cli.py htc -i board.hts
~~~

默认输出为
include/mrX/mr_hardgen.h。输出目录不存在时会自动创建；输入、包含文件或语义校验失败时不会写出新头文件，并会打印带文件和行号的诊断。若输出路径已有旧头文件，它会保留，不能把该旧文件当成本次生成成功的结果。

完整参数：

| 选项         | 默认值                   | 说明                               |
|--------------|--------------------------|------------------------------------|
| -i, --input  | 必填                     | 输入 .hts 或 .htsi 文件            |
| -o, --output | include/mrX/mr_hardgen.h | 输出头文件                         |
| `-I <目录>`  | 无                       | 额外包含搜索目录，可重复           |
| --no-cpp     | 关闭                     | 强制使用内置的有限预处理器         |
| --strict     | 关闭                     | 对 HTC 扩展语法给出 DTS 兼容性告警 |

示例：

~~~text
python ./scripts/cli.py htc -i boards/demo.hts -I boards/include -o build/include/mr_hardgen.h --no-cpp
~~~

默认模式会按顺序寻找 gcc、clang、cpp 并优先使用找到的 C 预处理器；找不到时自动
回退到内置预处理器。--no-cpp
可让不同机器使用一致的有限预处理行为。外部预处理器
已经报告语法错误时，脚本会直接报告错误，不会把错误输入当成成功结果。

## 编写 .hts 文件

下面的文件展示根节点、节点地址、标签、属性、引用和别名的常用组合：

~~~dts
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
~~~

### 语法速查

| 写法                             | 含义                                                |
|----------------------------------|-----------------------------------------------------|
| /dts-v1/;                        | 入口文件版本声明；包含文件中的重复声明会被忽略      |
| name { ... };                    | 子节点；结尾的 ; 在普通模式可省略，严格模式建议保留 |
| label: name@4000 { ... };        | 节点标签和不带 0x 的十六进制 unit address           |
| property;                        | 无值标志属性，生成值为 1 的宏                       |
| property = "text";               | 字符串属性                                          |
| `property = <1 2 3>;`            | 展开数组；数字、字符串和引用可作为元素              |
| property = [00 11 ff];           | DTS 字节串；每个元素必须是两位十六进制字节          |
| property = [1, 2, 3];            | 兼容旧写法的内联数组                                |
| property = &uart0;               | 节点引用                                            |
| &uart0 { ... };                  | 按标签修改已有节点                                  |
| &{/soc/serial@40000000} { ... }; | 按绝对路径修改已有节点                              |
| /delete-node/ target;            | 删除节点及其子树                                    |
| /delete-property/ name;          | 删除当前节点中的属性                                |

属性值支持字符串、十进制/十六进制/二进制/八进制数字、浮点数、标识符和节点引用。
节点引用可以写
&label、&{/absolute/path}，也可以在路径中使用 unit address。 compatible
应是字符串或字符串列表；只有带 compatible
且状态启用的节点会进入驱动 实例索引。

### 包含和条件编译

可以使用 C 风格包含，也可以使用 DTS 风格包含：

~~~dts
#include "soc.htsi"
/include/ "board-common.hts"
~~~

#include "..." 先查当前文件目录，再查 -I 目录；`#include <...>` 查 -I 目录。内置
预处理器支持对象式
#define、#undef、#if、#ifdef、#ifndef、#elif、#else 和 #endif。
包含路径必须是相对路径，不能使用绝对路径或 .. 穿越目录。

## status 与初始化顺序

status 缺省为 "okay"。HTC 还支持 mrX 用于驱动初始化排序的扩展值：

| 值                     | 生成的初始化级别或行为         |
|------------------------|--------------------------------|
| "early"                | 早期初始化                     |
| "okay"                 | 普通初始化                     |
| "last"                 | 最后初始化                     |
| "defer"                | 延后初始化                     |
| "prio(0)" 到 "prio(9)" | 指定优先级的初始化             |
| "disabled"             | 不生成该节点的节点宏或驱动实例 |

父节点被禁用时，子节点也按禁用处理。--strict 会提示 early、last、defer、prio (n)
、裸数字、浮点数和裸标识符等 HTC
扩展；不加该选项时这些语法是合法的 mrX 扩展。

## 生成结果如何被驱动使用

生成头文件包含三类信息：

1. 每个启用节点的 EXIST、NAME、PATH、INIT_LEVEL、PARENT、ADDR 和属性宏。
2. label 和 aliases 的符号映射。
3. 按 compatible 分组的实例数量和实例路径索引。

驱动通常通过 `<mrX/drivers/mr_driver.h>` 间接获得这些定义，再使用对应驱动文档中说明的
MR_HT_* 宏。应用修改硬件描述后，必须重新运行
HTC，不能手工改 mr_hardgen.h。

## 常见问题

| 现象                     | 处理方式                                                             |
|--------------------------|----------------------------------------------------------------------|
| Input file not found     | 检查 -i 路径；显式相对路径相对当前会话目录                           |
| include file not found   | 检查包含文件位置，并为其目录添加 -I                                  |
| 节点没有出现在驱动实例中 | 确认 compatible 是字符串/字符串列表，且节点和所有父节点不是 disabled |
| 引用解析失败             | 先声明目标节点或标签，或使用正确的绝对路径                           |
| 严格模式只有告警但仍成功 | --strict 的职责是兼容性提示；若需要纯 Linux DTS，按告警改写输入语法  |
| 没有安装 gcc/clang       | 使用 --no-cpp，但只依赖内置预处理器支持的指令                        |

## 脚本关系

htc.py 处理 CLI 参数和输出；compiler.py 依次调用预处理、词法分析、语法分析、语义构树、
校验和宏生成。frontend/、semantic/、validation/、backend/ 是编译器内部库，不单独执行。
