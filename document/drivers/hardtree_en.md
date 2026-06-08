[**中文**](drivers_hardtree.md) | [**English**](drivers_hardtree_en.md)

---

# HardTree

The HardTree module provides compile-time hardware tree access macros. It reads nodes, properties, instances, and array elements from macros generated in `mr_hardgen.h`. It is not a runtime tree; all queries are resolved through preprocessor expansion.

The mechanism follows a Zephyr-like devicetree macro style and is mainly used for driver installation, device spec construction, and platform configuration.

## Core Concepts

HardTree nodes are represented by macros generated in `mr_hardgen.h`. Callers usually do not concatenate generated macros directly. Instead, they use `MR_HT_*` macros to find nodes by path, label, alias, or compatible, then read node properties.

The common flow is:

```text
path / label / alias / compatible -> node -> property -> generated driver data
```

## Node Lookup Macros

### Path

```c
MR_HT_HAS_PATH(...)
MR_HT_PATH(...)
```

`MR_HT_HAS_PATH()` checks whether a path exists. `MR_HT_PATH()` returns the node macro from path fragments.

### Label and Alias

```c
MR_HT_HAS_LABEL(name)
MR_HT_LABEL(name)
MR_HT_HAS_ALIAS(name)
MR_HT_ALIAS(name)
```

Labels and aliases provide stable names for nodes. `MR_HT_LABEL()` and `MR_HT_ALIAS()` return the corresponding node.

### Compatible Instances

```c
MR_HT_HAS_INST(compat)
MR_HT_INST_NUM(compat)
MR_HT_INST(compat, id)
MR_HT_INST_FOREACH(compat, fn, ...)
```

Compatible instances enumerate nodes by device type. `MR_HT_INST_FOREACH()` expands one call for each instance and is useful for installing all drivers of the same type.

## Property Access Macros

### Normal Properties

```c
MR_HT_NODE_HAS_PROP(node, prop)
MR_HT_NODE_PROP(node, prop)
MR_HT_NODE_PROP_OR(node, prop, default)
```

`MR_HT_NODE_PROP()` reads a node property. `MR_HT_NODE_PROP_OR()` returns a default value when the property does not exist and is appropriate for optional hardware configuration.

### Property Array Elements

```c
MR_HT_NODE_HAS_PROP_ELEM(node, prop, elem, idx)
MR_HT_NODE_PROP_ELEM(node, prop, elem, idx)
MR_HT_NODE_PROP_ELEM_OR(node, prop, elem, idx, default)
MR_HT_NODE_PROP_ELEM_FOREACH(node, prop, fn, ...)
```

Property array elements describe multi-cell properties such as GPIO specs, ADC specs, and RAM regions. `MR_HT_NODE_PROP_ELEM_FOREACH()` expands one call for each element in a property array.

## Node Metadata

```c
MR_HT_NODE_NAME(node)
MR_HT_NODE_PATH(node)
MR_HT_NODE_HAS_PARENT(node)
MR_HT_NODE_PARENT(node)
MR_HT_NODE_HAS_ADDR(node)
MR_HT_NODE_ADDR(node)
```

These macros read node name, path, parent, and address metadata. Driver installation macros often use node path and address information to generate driver names or private data.

## Usage Example

```c
#if MR_HT_HAS_ALIAS(led0)
#define SAMPLE_LED_NODE MR_HT_ALIAS(led0)

static const char *sample_led_name = MR_HT_NODE_NAME(SAMPLE_LED_NODE);
#endif
```

This example finds the `led0` node through an alias and reads its node name.

```c
#define SAMPLE_INST_INIT(_id, ...) \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(gpio), \
                         MR_DRIVER_TYPE_GPIO, MR_NULL, MR_NULL, MR_NULL)

MR_HT_INST_FOREACH(gpio, SAMPLE_INST_INIT);
```

This example iterates over `gpio` compatible instances and expands a driver installation macro for each instance.

## Notes

- HardTree depends on macros generated in `mr_hardgen.h`; without the generated file, related macros cannot expand.
- `MR_HT_NODE_PROP()` requires the property to exist. Use `MR_HT_NODE_PROP_OR()` for optional properties.
- `MR_HT_NODE_PROP_ELEM_FOREACH()` is suitable for generating static array initializers; the expansion is decided by the preprocessor.
- HardTree does not provide runtime traversal or dynamic modification.
- The driver-level `MR_HT_DRIVER_INSTALL()` uses the node `INIT_LEVEL` property by default. If it does not exist, it uses `MR_DRIVER_LEVEL_DEFER`.
