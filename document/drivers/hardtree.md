[**中文**](drivers_hardtree.md) | [**English**](drivers_hardtree_en.md)

---

# HardTree

HardTree 模块提供编译期硬件树访问宏，用于从 `mr_hardgen.h` 生成的宏中获取节点、属性、实例和数组元素。它不是运行时树结构，所有查询都在预处理阶段通过宏展开完成。

该实现机制参考 Zephyr 的 devicetree 宏风格，主要供驱动安装、设备 spec 构造和平台配置使用。

## 核心概念

HardTree 节点由 `mr_hardgen.h` 生成的宏表示。调用方通常不会直接拼接生成宏，而是通过 `MR_HT_*` 宏按路径、label、alias 或 compatible 查找节点，再读取节点属性。

常见使用路径如下：

```text
path / label / alias / compatible -> node -> property -> generated driver data
```

## 节点查找宏

### 路径

```c
MR_HT_HAS_PATH(...)
MR_HT_PATH(...)
```

`MR_HT_HAS_PATH()` 判断路径是否存在。`MR_HT_PATH()` 根据路径片段返回节点宏。

### Label 和 Alias

```c
MR_HT_HAS_LABEL(name)
MR_HT_LABEL(name)
MR_HT_HAS_ALIAS(name)
MR_HT_ALIAS(name)
```

label 和 alias 用于给节点提供稳定名字。`MR_HT_LABEL()` 和 `MR_HT_ALIAS()` 返回对应节点。

### Compatible 实例

```c
MR_HT_HAS_INST(compat)
MR_HT_INST_NUM(compat)
MR_HT_INST(compat, id)
MR_HT_INST_FOREACH(compat, fn, ...)
```

compatible 实例用于按设备类型枚举节点。`MR_HT_INST_FOREACH()` 会对每个实例展开一次调用，适合批量安装同类驱动。

## 属性访问宏

### 普通属性

```c
MR_HT_NODE_HAS_PROP(node, prop)
MR_HT_NODE_PROP(node, prop)
MR_HT_NODE_PROP_OR(node, prop, default)
```

`MR_HT_NODE_PROP()` 读取节点属性。`MR_HT_NODE_PROP_OR()` 在属性不存在时返回默认值，常用于可选硬件配置。

### 属性数组元素

```c
MR_HT_NODE_HAS_PROP_ELEM(node, prop, elem, idx)
MR_HT_NODE_PROP_ELEM(node, prop, elem, idx)
MR_HT_NODE_PROP_ELEM_OR(node, prop, elem, idx, default)
MR_HT_NODE_PROP_ELEM_FOREACH(node, prop, fn, ...)
```

属性数组元素用于描述 GPIO spec、ADC spec、RAM region 等多 cell 属性。`MR_HT_NODE_PROP_ELEM_FOREACH()` 会对属性数组中的每个元素展开一次调用。

## 节点元信息

```c
MR_HT_NODE_NAME(node)
MR_HT_NODE_PATH(node)
MR_HT_NODE_HAS_PARENT(node)
MR_HT_NODE_PARENT(node)
MR_HT_NODE_HAS_ADDR(node)
MR_HT_NODE_ADDR(node)
```

这些宏用于读取节点名称、路径、父节点和地址。驱动安装宏通常会使用节点路径和地址生成 driver 名称或私有数据。

## 使用示例

```c
#if MR_HT_HAS_ALIAS(led0)
#define SAMPLE_LED_NODE MR_HT_ALIAS(led0)

static const char *sample_led_name = MR_HT_NODE_NAME(SAMPLE_LED_NODE);
#endif
```

该示例通过 alias 查找 `led0` 节点，并读取节点名称。

```c
#define SAMPLE_INST_INIT(_id, ...) \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(gpio), \
                         MR_DRIVER_TYPE_GPIO, MR_NULL, MR_NULL, MR_NULL)

MR_HT_INST_FOREACH(gpio, SAMPLE_INST_INIT);
```

该示例按 `gpio` compatible 遍历实例，并对每个实例展开驱动安装宏。

## 注意事项

- HardTree 依赖 `mr_hardgen.h` 生成的宏，缺少生成文件时相关宏无法展开。
- `MR_HT_NODE_PROP()` 要求属性存在；读取可选属性时应优先使用 `MR_HT_NODE_PROP_OR()`。
- `MR_HT_NODE_PROP_ELEM_FOREACH()` 适合生成静态数组初始化项，展开结果由预处理器决定。
- HardTree 不提供运行时遍历或动态修改能力。
- 驱动层的 `MR_HT_DRIVER_INSTALL()` 默认使用节点 `INIT_LEVEL` 属性；若不存在，则使用 `MR_DRIVER_LEVEL_DEFER`。
