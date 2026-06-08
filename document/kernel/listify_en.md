[**中文**](kernel_listify.md) | [**English**](kernel_listify_en.md)

---

# Macro Sequence Expansion

The `mr_listify` module provides preprocessor-time repeated expansion capability. Given the expansion count, separator,
callback macro, and additional arguments, `MR_LISTIFY()` generates a set of macro calls with incrementing indices.

This module is primarily used for static initialization and repeated declaration scenarios. For example, the linked list
array static initialization in `mr_list` uses `MR_LISTIFY()` to generate one `MR_LIST_INIT()` entry for each array
subscript.

## Core Macros

### Normal Sequence Expansion

```c
#define MR_LISTIFY(_n, _s, _f, ...) \
    MR_CAT(__MR_LISTIFY_, _n)(_s, _f, ##__VA_ARGS__)
```

The parameters of `MR_LISTIFY()` are as follows:

| Parameter | Meaning                                                                               |
|-----------|---------------------------------------------------------------------------------------|
| `_n`      | Expansion count, generates indices `0` to `_n - 1`.                                   |
| `_s`      | Separator between each expanded item, used via `MR_DE_BRACKET _s` to remove brackets. |
| `_f`      | The macro to invoke, invoked as `_f(index, ...)`.                                     |
| `...`     | Additional arguments passed to `_f`.                                                  |

The header defines `__MR_LISTIFY_0` through `__MR_LISTIFY_4096`. `__MR_LISTIFY_0` generates no content, `__MR_LISTIFY_1`
generates `_f(0, ...)`, and each subsequent version appends one item based on the previous version.

### Nested Sequence Expansion

```c
#define MR_LISTIFY_NEST(_n, _s, _f, ...) \
    MR_CAT(__MR_LISTIFY_NEST_, _n)(_s, _f, ##__VA_ARGS__)
```

`MR_LISTIFY_NEST()` uses the same parameter form as `MR_LISTIFY()`, but its internal macro name is
`__MR_LISTIFY_NEST_x`. The current header defines up to `__MR_LISTIFY_NEST_255`. It is used for macro expansion
scenarios that require a separate recursion layer from the normal `MR_LISTIFY()`.

## Main Interfaces

### Generate Normal Sequence Expansion

```c
MR_LISTIFY(_n, _s, _f, ...)
```

When `_n` is `3`, the separator is `(, )`, and the callback macro is `ITEM`, the expansion result is equivalent to:

```c
ITEM(0, ...), ITEM(1, ...), ITEM(2, ...)
```

If `_n` is `0`, no items are generated.

### Generate Nested Sequence Expansion

```c
MR_LISTIFY_NEST(_n, _s, _f, ...)
```

This interface also invokes `_f(index, ...)` in order from `0` to `_n - 1`, but uses the independent
`__MR_LISTIFY_NEST_x` macro family, suitable for reducing naming conflict risk when using normal sequence expansion
again inside a macro callback.

## Usage Example

The linked list array initialization in `mr_list` uses `MR_LISTIFY()` to generate multiple initialization items:

```c
#define MR_LISTS_INIT(_list, _list_num) \
    {MR_LISTIFY(_list_num, (, ), __MR_LIST_ARRAY_INIT_IMPL, _list)}

#define __MR_LIST_ARRAY_INIT_IMPL(_i, _list) \
    MR_LIST_INIT(&(_list)[_i])
```

For a linked list array with 3 elements, the generated result is equivalent to:

```c
{
    MR_LIST_INIT(&list[0]),
    MR_LIST_INIT(&list[1]),
    MR_LIST_INIT(&list[2])
}
```

Below is a standalone declaration generation example:

```c
#define DECL_BUF(_i, _prefix) static int _prefix##_buf##_i

MR_LISTIFY(3, (;), DECL_BUF, sample);
```

This macro call generates 3 declaration items in sequence, separated by semicolons.

## Notes

- `_n` must correspond to a macro number already defined in the header; the normal `MR_LISTIFY()` currently defines up
  to `4096`, and the nested `MR_LISTIFY_NEST()` currently defines up to `255`.
- `_s` is typically passed in bracketed form, such as `(, )` or `(;)`, with the outer brackets removed by
  `MR_DE_BRACKET` in the implementation.
- `_f` must accept an index as its first argument and accept additional arguments passed by the caller.
- Larger expansion counts increase preprocessing and compilation burden; avoid unnecessarily large expansions when
  statically initializing arrays.
- This module only performs preprocessor-time code generation and does not produce runtime data structures or runtime
  code logic.
