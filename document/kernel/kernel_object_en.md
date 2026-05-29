[**中文**](kernel_object.md) | [**English**](kernel_object_en.md)

---

# Object Management

The mrX kernel uses object control blocks to uniformly manage the lifecycle of kernel resources. Each kernel object has
an embedded `mr_object_t` as its base object, which records the reference count and class descriptor. Callers acquire a
reference via `mr_object_get()`, release it via `mr_object_put()` or `mr_object_del()`; when the last reference is
released, the kernel terminates the object lifecycle and calls the corresponding class release function.

This document describes the object control block, class descriptor, reference rules, and usage of common interfaces.

## Core Concepts

### Object Control Block

The object control block describes a kernel object:

```c
typedef struct mr_object {
    mr_ref_t magic_ref;
    const mr_clazz_t *clazz;
} mr_object_t;
```

Field meanings:

| Field       | Meaning                                                                                                                                                                         |
|-------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `magic_ref` | Reference count and lifecycle marker. It is managed by `mr_ref_t`; after initialization, the count is 1; after the last reference is released, the lifecycle marker is cleared. |
| `clazz`     | Class descriptor used by the current object. When the object is released, release functions are called along the inheritance chain of this class.                               |

Objects can be initialized dynamically, or statically using `MR_OBJECT_INIT()`:

```c
#define MR_OBJECT_INIT(_clazz)                                                 \
    {                                                                          \
        .magic_ref = MR_REF_INIT(),                                            \
        .clazz = (_clazz),                                                     \
    }
```

### Class Descriptor

The class descriptor describes the object type, release function, and parent class:

```c
typedef struct mr_clazz {
    const char *name;
    mr_object_release_t *release;
    const struct mr_clazz *super;
} mr_clazz_t;
```

Field meanings:

| Field     | Meaning                                                                                                                                    |
|-----------|--------------------------------------------------------------------------------------------------------------------------------------------|
| `name`    | Class name, primarily used to identify the object type.                                                                                    |
| `release` | Release function called when the object lifecycle ends; can be `MR_NULL`.                                                                  |
| `super`   | Parent class descriptor, used to establish inheritance relationships and release function chains. `MR_NULL` when there is no parent class. |

Class descriptors can be initialized statically using `MR_CLAZZ_INIT()` or at runtime using `mr_clazz_init()`.

### Class Family

A class family simultaneously describes the static and dynamic object classes for the same type of object:

```c
typedef struct mr_clazzes {
    const mr_clazz_t statik;
    const mr_clazz_t dynamic;
} mr_clazzes_t;
```

`statik` is the class descriptor for static objects, and `dynamic` is the class descriptor for dynamic objects. The
field name `statik` is used to avoid conflicting with the C/C++ `static` keyword.

Class families are typically defined and accessed using the following macros:

```c
MR_CLAZZES_DEFINE(timer, timer_statik_release, timer_dynamic_release, MR_NULL);

MR_CLAZZES_STATIK(timer);
MR_CLAZZES_DYNAMIC(timer);
```

Static and dynamic classes can use different release functions. Static release functions typically only reclaim internal
resources of the object without freeing the memory occupied by the object itself; dynamic release functions usually free
the memory occupied by the object itself after reclaiming internal resources.

This distinction embodies an important principle: whoever allocates memory should free it. If an object is dynamically
created, only the class that actually performs the dynamic creation is responsible for freeing the entire object memory;
its parent classes only free the internal resources they manage, following the static object approach.

## Lifecycle Rules

The object lifecycle follows these rules:

1. Objects must be initialized before being passed to other object interfaces.
2. After successful initialization, the object's initial reference count is 1.
3. Each successful call to `mr_object_get()` increments the reference by one; the caller must match it with one
   `mr_object_put()` or `mr_object_del()` to release that reference.
4. `mr_object_put()` returns `MR_TRUE` when releasing the last reference and triggers the class release function chain.
5. `mr_object_put()` returns `MR_FALSE` when releasing a non-last reference; the object remains valid.
6. After the last reference is released, the `magic_ref` lifecycle marker is cleared, and subsequent `mr_object_get()`
   calls will fail.
7. `mr_object_put(MR_NULL)`, `mr_object_put()` on an uninitialized object or an object whose lifecycle has ended returns
   `MR_TRUE`, indicating the caller no longer needs to hold the object.
8. `mr_object_del()` is a convenience interface that ignores the return value, suitable for scenarios where the caller
   does not care whether the object's lifecycle ends after this call.

Reference count increments and decrements are performed atomically by `mr_ref_t`, but the object's class pointer update,
release function content, and object resources are not automatically locked. If an object may be concurrently derived,
released, or have its internal resources accessed by multiple execution contexts, the caller must use the lock belonging
to the object's module to protect these operations.

## Class Inheritance

`super` is used to establish the class inheritance chain. `mr_clazz_is_kind_of(clazz, target)` traverses upward along
`clazz->super`; if `target` exists on the chain, `clazz` is considered the same class as or a subclass of `target`.

The inheritance relationship is essentially linked by class descriptor pointers. For example, the following class family
definition indicates that `child` inherits from `base`:

```c
MR_CLAZZES_DEFINE(base, base_statik_release, base_dynamic_release, MR_NULL);
MR_CLAZZES_DEFINE(child, child_statik_release, child_dynamic_release,
                  MR_CLAZZES_STATIK(base));
```

The static class chain is then:

```text
child.statik -> base.statik -> MR_NULL
```

Note that the fourth parameter of `MR_CLAZZES_DEFINE()` is the parent class descriptor and simultaneously serves as the
parent for both the `statik` and `dynamic` class descriptors. In the example above, `child`'s parent is `base.statik`,
so both `child.statik` and `child.dynamic` inherit from `base.statik`.

This design is intentional: a child object's memory is typically a single contiguous block of memory; the parent part is
not separately `create`d but is constructed within the child object's memory by calling the parent's initialization
logic. Therefore, when releasing, the parent class should be released as a static object, only cleaning up the internal
resources owned by the parent; only the outermost dynamic subclass release function that actually allocated the entire
object memory should free the object memory itself.

If static and dynamic classes need to inherit from different parent classes, `MR_CLAZZES_DEFINE()` cannot be used for a
one-time definition; the corresponding `mr_clazz_t` values must be constructed separately. However, in conventional
object inheritance, a dynamic subclass inheriting from a static parent better fits the model where "the parent class
only initializes and does not allocate the entire object memory."

### Type Determination

`mr_clazz_is_kind_of(clazz, target)` determines whether a class belongs to the target class:

```c
mr_clazz_is_kind_of(MR_CLAZZES_STATIK(child), MR_CLAZZES_STATIK(base));
```

The call above returns `MR_TRUE` because `base.statik` exists on the parent chain of `child.statik`.

Determination rules:

| Call                                 | Result     | Reason                                               |
|--------------------------------------|------------|------------------------------------------------------|
| `mr_clazz_is_kind_of(base, base)`    | `MR_TRUE`  | A class matches itself.                              |
| `mr_clazz_is_kind_of(child, base)`   | `MR_TRUE`  | The parent class exists on the child's chain.        |
| `mr_clazz_is_kind_of(base, child)`   | `MR_FALSE` | A parent class does not reverse-match a child class. |
| `mr_clazz_is_kind_of(MR_NULL, base)` | `MR_FALSE` | A null class has no inheritance chain.               |

`MR_OBJECT_IS_CLAZZES(obj, name)` is an object-level type determination macro. It compares the object's current class
against both the static and dynamic classes of `name`, returning `MR_TRUE` if either class family branch matches.

### Object Derivation

`mr_object_derive()` can only derive an object from its current class to a subclass of the current class:

```c
mr_object_init(&obj, MR_CLAZZES_STATIK(base));
mr_object_derive(&obj, MR_CLAZZES_STATIK(child));
```

After derivation succeeds, the object's `clazz` is updated from the original class descriptor to the target class
descriptor. The reference count does not change, and the object memory is not reallocated.

The derivation direction must satisfy: the target class is the same as or a subclass of the current class. That is, the
following derivations are valid:

```c
mr_object_derive(&obj, MR_CLAZZES_STATIK(base));
mr_object_derive(&obj, MR_CLAZZES_STATIK(child));
```

The following derivations are invalid:

```c
mr_object_derive(&obj, MR_CLAZZES_STATIK(unrelated));
mr_object_derive(&obj, MR_CLAZZES_STATIK(parent_of_current));
```

Invalid derivation returns `-MR_EPERM`. When the object is null, the target class is null, or the object is not
initialized, `-MR_EINVAL` is returned.

The derive interface is suitable for expressing "object initialization completes the common part first, then advances to
a more specific type." It is not suitable for arbitrarily changing the type after the object has been exposed to other
threads; if such a change is necessary, the caller must ensure no concurrent access or use an external lock for
protection.

From a memory model perspective, derivation does not create new parent or child class objects, nor does it move object
memory. It simply updates the `clazz` in the object control block to a more specific class descriptor. Actual parent
class initialization should occur inside the child class construction flow and typically follows the parent's `init`
path, not the parent's `create` path.

### Release Order

When an object is released, release functions are called in order from parent class to child class. That is, if the
object's current class is `child` and `child->super` is `base`, the release order is:

1. `base->release(obj)`
2. `child->release(obj)`

A release function can be `MR_NULL`, in which case no release callback is executed for that class level, but other
release functions on the inheritance chain still continue to execute.

This order comes from the recursive release logic in the current implementation: the `clazz->super` is released
recursively first, then the current `clazz->release` is called. Therefore, if a child class depends on the parent class
release function completing common resource reclamation first, this order is safe; however, if a child class release
function needs to use parent class resources, it cannot assume those parent resources are still available at the time of
child class release.

When releasing a dynamic subclass object, the parent classes in the inheritance chain typically use static class release
functions, and the child class's own dynamic release function is called last. For example, the release chain
`child.dynamic -> base.statik -> MR_NULL` will execute `base`'s static release first, then `child`'s dynamic release.
This ensures that the parent class does not mistakenly free the entire object memory allocated by the child class, and
the `child`'s dynamic release function ultimately completes the entire memory release.

When designing release functions, the following conventions are recommended:

- Parent class release functions only handle common resources owned by the parent.
- Child class release functions only handle resources newly added by the child.
- When a parent class is used as `super`, static class descriptors should be preferred.
- Dynamic release functions should only appear at the layer that actually allocated the entire object memory.
- Child class release functions should not depend on resource state after parent class release functions have executed.
- Do not call `mr_object_put()` again to release the same object reference within a release function.

## Object Management Interfaces

### Initializing an Object

```c
mr_err_t mr_object_init(mr_object_t *obj, const mr_clazz_t *clazz);
```

`mr_object_init()` saves the class descriptor to the object control block and initializes the reference count to 1.

Return values:

| Return Value | Meaning                                                   |
|--------------|-----------------------------------------------------------|
| `0`          | Initialization succeeded.                                 |
| `-MR_EINVAL` | `obj` is `MR_NULL`, or the object is already initialized. |

`clazz` can be `MR_NULL`. Such an object can still use reference counting to manage its lifecycle, but no class release
function will be called when the last reference is released.

### Deriving an Object

```c
mr_err_t mr_object_derive(mr_object_t *obj, const mr_clazz_t *clazz);
```

`mr_object_derive()` updates the object's current class to the specified subclass. This interface first checks whether
the object is already initialized, then checks whether the target class is the same as or a subclass of the current
class.

Return values:

| Return Value | Meaning                                                                         |
|--------------|---------------------------------------------------------------------------------|
| `0`          | Derivation succeeded.                                                           |
| `-MR_EINVAL` | `obj` is `MR_NULL`, `clazz` is `MR_NULL`, or the object is not yet initialized. |
| `-MR_EPERM`  | `clazz` is not the same as or a subclass of the object's current class.         |

Derivation is typically used when a base object first completes generic initialization, and then a more specific module
advances the object type to a subclass. Do not modify the class descriptor of an object after it may already be released
by another execution context.

### Acquiring a Reference

```c
mr_object_t *mr_object_get(mr_object_t *obj);
```

`mr_object_get()` attempts to increment the object's reference count.

Return values:

| Return Value   | Meaning                                                                                 |
|----------------|-----------------------------------------------------------------------------------------|
| Object pointer | Reference acquisition succeeded.                                                        |
| `MR_NULL`      | `obj` is `MR_NULL`, the object is not initialized, or the object's lifecycle has ended. |

The caller only holds the new reference when a non-`MR_NULL` value is returned, and must match it with one
`mr_object_put()` or `mr_object_del()`.

### Releasing a Reference

```c
mr_bool_t mr_object_put(mr_object_t *obj);
```

`mr_object_put()` releases one reference of the object.

Return values:

| Return Value | Meaning                                                                                                                         |
|--------------|---------------------------------------------------------------------------------------------------------------------------------|
| `MR_TRUE`    | The object's lifecycle has ended in this call, or the passed object is null, uninitialized, or its lifecycle has already ended. |
| `MR_FALSE`   | Only a non-last reference was released; the object still has other references.                                                  |

When the last reference is released, `mr_object_put()` clears the lifecycle marker and calls the release chain of the
object's current class.

### Convenience Delete

```c
void mr_object_del(mr_object_t *obj);
```

`mr_object_del()` releases one reference and ignores whether the lifecycle has ended. Its behavior is equivalent to:

```c
(void)mr_object_put(obj);
```

If `obj` is `MR_NULL` or the object is not initialized, the function returns directly.

## Usage Examples

### Basic Reference Counting

The following example demonstrates the basic process of initializing, acquiring, and releasing references:

```c
#include <mrX/kernel/mr_object.h>

static void sample_release(mr_object_t *obj)
{
    (void)obj;
}

static const mr_clazz_t sample_clazz =
    MR_CLAZZ_INIT("sample", sample_release, MR_NULL);

void sample_object_usage(void)
{
    mr_object_t obj;

    if (mr_object_init(&obj, &sample_clazz) != 0) {
        return;
    }

    if (mr_object_get(&obj) != MR_NULL) {
        mr_object_put(&obj);
    }

    mr_object_del(&obj);
}
```

In this example, `obj` is a statically allocated object. After successful initialization, the reference count is 1;
after `mr_object_get()` succeeds, the reference count increases to 2; then `mr_object_put()` releases the new reference,
and `mr_object_del()` releases the reference held from initialization and triggers `sample_release()`.

### Class Family and Derivation

The following example demonstrates the use of class families and derivation:

```c
#include <mrX/kernel/mr_object.h>

static void base_release(mr_object_t *obj)
{
    (void)obj;
}

static void child_release(mr_object_t *obj)
{
    (void)obj;
}

MR_CLAZZES_DEFINE(base, base_release, base_release, MR_NULL);
MR_CLAZZES_DEFINE(child, child_release, child_release,
                  MR_CLAZZES_STATIK(base));

void sample_derive_usage(void)
{
    mr_object_t obj;

    if (mr_object_init(&obj, MR_CLAZZES_STATIK(base)) != 0) {
        return;
    }

    if (mr_object_derive(&obj, MR_CLAZZES_STATIK(child)) != 0) {
        mr_object_del(&obj);
        return;
    }

    mr_object_del(&obj);
}
```

The object is first initialized with the `base` class, then derived to the `child` class. When the object is finally
released, the release chain calls `base_release()` first, then `child_release()`.

## Usage Recommendations

- The object creator should clearly define who holds the initialization reference and when that reference should be
  released.
- When reference acquisition fails, do not continue accessing the object's internal resources.
- Release functions should only handle resources that need to be reclaimed when the object's lifecycle ends; they should
  not release the same object reference again.
- Static and dynamic objects should use class families to distinguish release behavior, preventing the static object
  release path from mistakenly freeing memory.
- Derivation should occur before the object is published externally, or under external synchronization protection.
