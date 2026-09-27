# 启动与固件更新（Boot）

> Boot 模块提供面向嵌入式设备的 A/B 启动、受控固件更新、回退、Recovery 与 Rescue
> 策略。存储访问、复位来源、跳转、Hash、签名验证、State 认证与防回滚计数器均由平台
> 通过 `mr_boot_ops_t` 注入，Boot Core 不直接依赖具体 Flash、CPU 或密码硬件。

| 项       | 说明                                                                                                                                                                                  |
|----------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_MODULE_BOOT`；可选 `MR_USE_MODULE_BOOT_SHA256`、`MR_USE_MODULE_BOOT_ECDSA_P256`、`MR_USE_MODULE_BOOT_RECOVERY`、`MR_USE_MODULE_BOOT_AUTH`、`MR_USE_MODULE_BOOT_FAST`          |
| 头文件   | `<mrX/modules/boot/mr_boot.h>`                                                                                                                                                        |
| 依赖模块 | [对象系统 Object](../../kernel/object.md)、[内存分配](../../libc/malloc.md)（仅 `mr_boot_create` 动态方式）；密码算法通过 ops 注入，可使用 [Crypto](../crypto/crypto.md) 实现 backend |
| 参考测试 | `mrX/test/modules/boot/boot.c`                                                                                                                                                        |

---

## 用户指南

### 1. 模块概述

Boot 管理两份可启动 APP 固件和可选的一份 Recovery 固件：

```text
                  +------------------+
                  |       Boot       |
                  +------------------+
                    |      |      |
                    v      v      v
                 APP A   APP B  Recovery
```

核心目标是：

- 正常上电时优先启动当前 active APP；不可用时自动尝试另一 APP。
- WATCHDOG 复位时立即判废当前 active APP，再尝试另一 APP。
- 外部升级写入 inactive APP 后，可用 `MR_BOOT_SOURCE_UPDATE` 请求验证并切换。
- 当前 APP 主动请求回退时，可用 `MR_BOOT_SOURCE_ROLLBACK` 切换到另一 APP。
- 两个 APP 都不可用时进入 Recovery；Recovery 不存在或启动失败时进入 Rescue。
- Rescue 不是一份固件，而是留在 Boot 上层，不执行 `jump()`。
- 可选 SHA-256 校验、ECDSA P-256 manifest 验签、State 认证、防回滚和 FAST 快速验证。
- Boot 不包含网络协议栈；网络 OTA、串口升级协议或本地维护 UI 均由上层实现。

Boot 管理的是 **启动策略与可信状态**，不是通用 Flash 驱动。所有物理存储访问都通过
`mr_boot_ops_t` 完成，因此同一套 Core 可以落在内部 Flash、外部 NOR 或其他可寻址存储上。

### 2. 配置与依赖

| 配置项                           | 类型 | 默认值 | 说明                                                 |
|----------------------------------|------|--------|------------------------------------------------------|
| `MR_USE_MODULE_BOOT`             | bool | n      | 启用 Boot 模块                                       |
| `MR_USE_MODULE_BOOT_SHA256`      | bool | y      | 对 firmware image 做 SHA-256 完整性校验              |
| `MR_USE_MODULE_BOOT_ECDSA_P256`  | bool | y      | 对 manifest 做 ECDSA P-256 验签；自动选择 SHA-256    |
| `MR_USE_MODULE_BOOT_RECOVERY`    | bool | n      | 启用独立 Recovery firmware 区域                      |
| `MR_USE_MODULE_BOOT_AUTH`        | bool | y      | 对持久化 State 做认证                                |
| `MR_USE_MODULE_BOOT_FAST`        | bool | n      | manifest 未变化时跳过完整 image/hash/signature 校验  |
| `MR_CFG_BOOT_VERIFY_BUFFER_SIZE` | int  | 256    | 从存储分块重算 image hash 时的工作缓冲，范围 64~4096 |

包含公开头文件：

```c
#include <mrX/modules/boot/mr_boot.h>
```

Boot 不直接调用 mrX Flash 或 Crypto API。平台只需要按启用的功能实现对应 ops：

- `read/program/erase`：存储访问。
- `hash_begin/hash_update/hash_end`：SHA-256 流式计算。
- `verify`：ECDSA P-256 或等价 backend 的签名验证。
- `auth`：State 认证标签计算，secret 由 backend 持有。
- `source`：把复位原因或可信升级请求映射成 `mr_boot_source_t`。
- `rollback_read/rollback_update`：可选硬件单调计数器。
- `jump`：平台相关的 firmware 跳转。
- `sn_read/sn_write`：可选设备序列号读写。

`read` 是初始化阶段的基础能力，因为 `mr_boot_init()` 会立即加载持久化 State。 启用 AUTH 后，加载 State 时也会使用 `auth`
。其余能力按实际 API 路径使用，不要求平台 为了不用的功能提供空实现。

### 3. 存储布局

每个 APP / Recovery slot 的格式一致：

```text
slot_offset
    |
    +-- mr_boot_manifest_t
    |
    +-- firmware image
        image_size bytes
```

State 使用两个独立区域保存同一份 `mr_boot_state_t`：

```text
state_a_offset  -> State copy A
state_b_offset  -> State copy B
```

`mr_boot_config_t` 指定所有区域：

```c
typedef struct mr_boot_config {
    mr_uint32_t product_id;
    mr_uint64_t slot_a_offset;
    mr_uint64_t slot_a_size;
    mr_uint64_t slot_b_offset;
    mr_uint64_t slot_b_size;
#if defined(MR_USE_MODULE_BOOT_RECOVERY)
    mr_uint64_t recovery_offset;
    mr_uint64_t recovery_size;
#endif
    mr_uint64_t state_a_offset;
    mr_uint64_t state_b_offset;
    mr_uint64_t state_size;
} mr_boot_config_t;
```

约束：

- A/B slot 至少能容纳一个 `mr_boot_manifest_t`。
- State 区至少能容纳一个 `mr_boot_state_t`。
- Recovery 开启时同样至少能容纳 manifest。
- A、B、Recovery、State A、State B 之间不得重叠。
- Boot 不假设“擦除后的字节一定是 0xff”；是否存在有效对象由结构和认证结果判断。
- image 的最大长度为 `slot_size - sizeof(mr_boot_manifest_t)`。

### 4. Manifest 与固件验证

`mr_boot_manifest_t` 是持久化协议结构：

```c
typedef struct mr_boot_manifest {
    mr_uint32_t product_id;
    mr_uint32_t firmware_version;
    mr_uint32_t security_version;
    mr_uint64_t image_size;
    mr_uint8_t key_id;
    mr_uint8_t image_hash[MR_BOOT_SHA256_SIZE];
    mr_uint8_t signature[MR_BOOT_ECDSA_P256_SIZE];
} MR_PACKED mr_boot_manifest_t;
```

字段语义：

- `product_id`：必须与 `mr_boot_config_t.product_id` 一致。
- `firmware_version`：供产品侧标记版本；当前 Boot Core 不参与大小比较或启动决策。
- `security_version`：防回滚安全版本，APP 不允许低于当前安全下限。
- `image_size`：manifest 后 firmware image 的字节数，必须非 0 且不能越过 slot。
- `key_id`：交给 `ops->verify()` 选择可信公钥。
- `image_hash`：firmware image 的 SHA-256。
- `signature`：manifest 签名。

启用 ECDSA 时，签名输入是：

```text
SHA256(manifest 从首字节到 signature 字段之前的全部字节)
```

因此签名覆盖 `product_id`、两个 version、`image_size`、`key_id` 和
`image_hash`，但不包含 `signature` 自身。

启用 SHA-256 后，启动验证会从存储重新读取 image 并计算 hash。受控升级结束时也会在写入 manifest、切换 active slot 之前重新读取刚编程的
image，因此即使底层 `program()` 返回 完整长度但实际 Flash 数据损坏，也不会提交该固件。

### 5. 快速上手

下面展示静态对象的最小接线骨架。回调实现由平台提供；这些原型与
`mr_boot_ops_t` 完全一致。

```c
#include <mrX/modules/boot/mr_boot.h>

#define APP_A_OFFSET     (0x00100000ULL)
#define APP_B_OFFSET     (0x00180000ULL)
#define APP_SLOT_SIZE    (0x00080000ULL)
#define RECOVERY_OFFSET  (0x00200000ULL)
#define STATE_A_OFFSET   (0x00280000ULL)
#define STATE_B_OFFSET   (0x00281000ULL)
#define STATE_SIZE       (0x00001000ULL)

static mr_int64_t boot_read(mr_boot_t *boot, mr_uint64_t offset,
                            void *data, mr_uint64_t size);
static mr_int64_t boot_program(mr_boot_t *boot, mr_uint64_t offset,
                               const void *data, mr_uint64_t size);
static mr_err_t boot_erase(mr_boot_t *boot, mr_uint64_t offset,
                           mr_uint64_t size);
static mr_boot_source_t boot_source(mr_boot_t *boot);
static mr_err_t boot_jump(mr_boot_t *boot, mr_uint64_t offset);

#if defined(MR_USE_MODULE_BOOT_SHA256)
static mr_err_t boot_hash_begin(mr_boot_t *boot);
static mr_err_t boot_hash_update(mr_boot_t *boot, const void *data,
                                 mr_size_t size);
static mr_err_t boot_hash_end(mr_boot_t *boot,
                              mr_uint8_t digest[MR_BOOT_SHA256_SIZE]);
#endif

#if defined(MR_USE_MODULE_BOOT_ECDSA_P256)
static mr_err_t boot_verify(
    mr_boot_t *boot, mr_uint8_t key_id,
    const mr_uint8_t digest[MR_BOOT_SHA256_SIZE],
    const mr_uint8_t signature[MR_BOOT_ECDSA_P256_SIZE]);
#endif

#if defined(MR_USE_MODULE_BOOT_AUTH)
static mr_err_t boot_auth(mr_boot_t *boot, const void *data, mr_size_t size,
                          mr_uint8_t auth[MR_BOOT_SHA256_SIZE]);
#endif

static mr_boot_t boot;

static const mr_boot_config_t boot_config = {
    .product_id = 0x12345678U,
    .slot_a_offset = APP_A_OFFSET,
    .slot_a_size = APP_SLOT_SIZE,
    .slot_b_offset = APP_B_OFFSET,
    .slot_b_size = APP_SLOT_SIZE,
#if defined(MR_USE_MODULE_BOOT_RECOVERY)
    .recovery_offset = RECOVERY_OFFSET,
    .recovery_size = APP_SLOT_SIZE,
#endif
    .state_a_offset = STATE_A_OFFSET,
    .state_b_offset = STATE_B_OFFSET,
    .state_size = STATE_SIZE,
};

static const mr_boot_ops_t boot_ops = {
    .read = boot_read,
    .program = boot_program,
    .erase = boot_erase,
#if defined(MR_USE_MODULE_BOOT_SHA256)
    .hash_begin = boot_hash_begin,
    .hash_update = boot_hash_update,
    .hash_end = boot_hash_end,
#endif
#if defined(MR_USE_MODULE_BOOT_AUTH)
    .auth = boot_auth,
#endif
#if defined(MR_USE_MODULE_BOOT_ECDSA_P256)
    .verify = boot_verify,
#endif
    .source = boot_source,
    .jump = boot_jump,
};

static mr_err_t boot_main(void) {
    mr_err_t ret;

    ret = mr_boot_init(&boot, &boot_config, &boot_ops, MR_NULL);
    if (ret != 0) {
        return ret;
    }

    return mr_boot_start(&boot);
}
```

成功的 `jump()` 应当不返回。若 `jump()` 返回，Boot 将其视为该 firmware 没有成功运行， 并继续执行 fallback 策略。

### 6. 受控更新

受控更新始终写入当前 **inactive APP slot**，调用顺序为：

```text
mr_boot_update_begin()
        |
        v
mr_boot_update_write()  x N
        |
        v
mr_boot_update_end()
```

典型调用：

```c
mr_err_t update_image(mr_boot_t *boot, mr_boot_manifest_t *manifest,
                      const void *image, mr_size_t size) {
    mr_err_t ret;

    ret = mr_boot_update_begin(boot, manifest);
    if (ret != 0) {
        return ret;
    }

    ret = mr_boot_update_write(boot, image, size);
    if (ret != 0) {
        return ret;
    }

    ret = mr_boot_update_end(boot);
    if (ret != 0) {
        mr_boot_update_abort(boot);
        return ret;
    }
    return 0;
}
```

调用前，`manifest` 必须已经包含正确的 `image_size`、`image_hash` 和签名。

`mr_boot_update_begin()` 的重要行为：

1. 拒绝已存在的 update session。
2. 校验 product、image size 和 security version。
3. 选择 inactive APP slot 并检查容量。
4. ECDSA 开启时先验证 manifest 签名， **签名无效不会擦除目标 slot**。
5. 擦除整个目标 slot。
6. SHA-256 开启时开始流式 image hash。
7. 建立 update session。

`mr_boot_update_write()`：

- 只允许顺序追加，不接受随机 offset。
- 不允许写过 manifest 声明的 `image_size`。
- 每段先 program 到存储，再更新流式 hash。
- 底层短写按 `-MR_EIO` 处理。
- program/hash backend 失败后当前 update session 失效。

`mr_boot_update_end()`：

1. 要求累计写入量等于 `image_size`。
2. 对流式输入 hash 做第一次比对。
3. 从存储重新读取完整 image，再做第二次 hash 比对。
4. 准备新的 State：新 slot 变为 active，并清除该 slot 的 invalid 标志。
5. FAST 开启时保存完整 manifest digest。
6. image 完整验证通过后才 program manifest。
7. 再写两份 State，完成提交。

manifest 是 slot 内最后提交的内容；随后才提交 State。这个顺序意味着 image 写一半时突然 复位，不会把该半成品当成一个已经提交的新
firmware。

若更新中途不再继续，可调用：

```c
mr_boot_update_abort(boot);
```

abort 只清除 RAM 中的 update context，不额外擦除目标区域；下一次
`mr_boot_update_begin()` 会重新擦除目标 slot。

### 7. 启动源与策略

`ops->source()` 返回以下来源：

| Source                    | 行为                                                                           |
|---------------------------|--------------------------------------------------------------------------------|
| `MR_BOOT_SOURCE_POWER_ON` | 启动 active APP；失败后另一 APP → Recovery → Rescue                            |
| `MR_BOOT_SOURCE_RESTART`  | 与 POWER_ON 相同                                                               |
| `MR_BOOT_SOURCE_WATCHDOG` | 先把当前 active APP 标为 invalid，再尝试另一 APP → Recovery → Rescue           |
| `MR_BOOT_SOURCE_UPDATE`   | 先验证 inactive APP；成功则切换 active，明确无效则标 invalid；失败时继续旧 APP |
| `MR_BOOT_SOURCE_ROLLBACK` | 验证另一 APP，标废旧 active，切换并启动另一 APP                                |
| `MR_BOOT_SOURCE_RECOVERY` | 直接进入 Recovery；未启用 Recovery 时返回 `-MR_ENOSYS`                         |
| `MR_BOOT_SOURCE_RESCUE`   | 不跳任何 firmware，清空 launch 并返回 0                                        |

普通启动的搜索路径：

```text
active APP
    |
    | unavailable / invalid
    v
other APP
    |
    | unavailable / invalid
    v
Recovery
    |
    | unavailable / jump returned
    v
Rescue
```

如果 fallback 成功切到另一 APP，Boot 会先持久化新的 `active_slot`，再执行 `jump()`。

WATCHDOG 是激进策略：它表示“上一次 active APP 已经不可接受”，因此当前 active 会在尝试 另一 APP 前立即持久化为 invalid。

UPDATE source 用于“外部可信写者已经把新固件放到 inactive slot”的场景。若 staged manifest 明确返回 `-MR_EINVAL` 或
`-MR_EPERM`，该 slot 会被持久化为 invalid；存储 I/O 或 crypto backend 临时故障不会被永久解释成“镜像坏”。

### 8. Recovery 与 Rescue

Recovery 是可选的独立 firmware slot，使用与 APP 相同的 manifest + image 格式。

与 APP 不同：

- Recovery 不读取 A/B 的 invalid 标志。
- Recovery 不受 APP `security_version` 下限限制。
- 启动 Recovery 不改写 A/B active/invalid State。
- Recovery 的 `jump()` 若返回，Boot 进入 Rescue。
- Recovery 镜像仍会检查 product 与 size；未命中 FAST cache 时，按已启用功能继续做 ECDSA 和 SHA-256 校验。

Rescue 不对应任何 firmware。进入 Rescue 后：

```c
boot->launch.slot == MR_BOOT_SLOT_MAX
```

并且 `mr_boot_start()` 返回 0。上层可以在这个状态进入串口刷机、本地 USB DFU、维修命令行 或其他最小恢复服务。

### 9. State、invalid 与双副本

`mr_boot_state_t` 保存：

- `sequence`：State 版本序号，支持 32-bit 回绕比较。
- `security_version`：当前 APP 安全版本下限。
- `invalid[]`：slot 是否已被判废。
- `active_slot`：当前 active APP，只允许 A 或 B。
- `manifest_digest[][]`：FAST 开启时的 manifest cache。
- `auth`：AUTH 开启时的 State 认证标签。

启动时同时读取 State A / State B：

- 两份都合法时选择较新的 sequence。
- sequence 相同则选择后读取的副本。
- 只有一份合法时使用合法副本。
- 没有合法 State 且没有底层读取错误时，从 `active_slot = A`、其余为 0 的默认 State 开始。
- 有真实存储错误且没有任何合法副本时，初始化失败，不把 I/O 错误伪装成“空 State”。

保存 State 时先写 A，再写 B；`sequence` 每次提交递增。掉电时至少保留一份可恢复副本是这一 布局的主要目的。

### 10. 防回滚

APP manifest 的：

```c
security_version
```

不得低于 Boot 当前的安全下限，否则 update/select 返回 `-MR_EPERM`。Recovery 不执行这条 APP 安全版本限制。

硬件 rollback counter 是可选能力：

- `rollback_read` 和 `rollback_update` **必须成对存在**；缺任意一个时，Boot 使用持久化 State 中的 `security_version`
  作为软件安全下限。
- 两个 callback 都存在时，初始化会读取硬件 counter，并把它作为运行时安全版本。
- AUTH 开启且加载到经过认证的 State 时，如果 State 的 `security_version` 高于硬件 counter，Boot 会调用 `rollback_update`
  把硬件 counter 提升到该值。
- AUTH 关闭时，持久化 State 不被当成可信来源来抬高硬件 counter。

因此采用硬件 rollback counter 的平台应明确谁有权限推进 counter，并保证
`rollback_update()` 只允许单调增加。

### 11. AUTH 与 FAST 的安全边界

#### AUTH

`MR_USE_MODULE_BOOT_AUTH` 只认证 **可变 State**。认证范围是
`mr_boot_state_t` 从首字节到 `auth` 字段之前的所有字节。

`ops->auth()` 负责实际算法和 secret 管理。Boot Core 不持有 HMAC key，也不要求 backend 必须是软件 HMAC；安全芯片、TrustZone
服务或 OTP 派生都可以作为实现。

AUTH 不替代 image SHA-256 或 manifest 签名。

#### FAST

`MR_USE_MODULE_BOOT_FAST` 在受控更新成功时缓存：

```text
SHA256(完整 mr_boot_manifest_t，包括 signature)
```

启动时，如果当前存储中的完整 manifest digest 与缓存一致，Boot 直接信任之前的完整验证结果， 跳过 ECDSA 和 firmware image 全量
hash，从而减少大镜像启动延迟。

因此 FAST 的安全性依赖 **State 本身可信**。如果攻击者可以任意修改 State，应启用 AUTH 或使用 硬件写保护/安全区保护
State；否则攻击者可能伪造缓存状态，使 FAST 的信任前提失效。

### 12. SN 接口

Boot 只透传设备序列号读写：

```c
mr_ssize_t mr_boot_sn_read(mr_boot_t *boot, void *data, mr_size_t size);
mr_ssize_t mr_boot_sn_write(mr_boot_t *boot, const void *data, mr_size_t size);
```

未提供对应 callback 时返回 `-MR_ENOSYS`。SN 不参与启动选择、签名或 State 认证。

### 13. API 参考

#### 13.1 生命周期

| API              | 说明                             |
|------------------|----------------------------------|
| `mr_boot_init`   | 初始化静态 Boot 对象并加载 State |
| `mr_boot_create` | 动态分配并初始化 Boot 对象       |
| `mr_boot_del`    | 归还对象初始引用                 |
| `mr_boot_get`    | 增加对象引用                     |
| `mr_boot_put`    | 释放对象引用                     |

```c
mr_err_t mr_boot_init(mr_boot_t *boot, const mr_boot_config_t *config,
                      const mr_boot_ops_t *ops, void *priv);

mr_boot_t *mr_boot_create(const mr_boot_config_t *config,
                          const mr_boot_ops_t *ops, void *priv);

mr_err_t mr_boot_del(mr_boot_t *boot);
```

`config` 在初始化时复制到对象内部；`ops` 与 `priv` 只保存指针，调用方必须保证它们在 Boot 最终 release 前持续有效。

#### 13.2 更新

| API                    | 说明                                              |
|------------------------|---------------------------------------------------|
| `mr_boot_update_begin` | 校验 manifest、选择并擦除 inactive slot，开始更新 |
| `mr_boot_update_write` | 顺序写入下一段 image                              |
| `mr_boot_update_end`   | 校验存储中的 image、提交 manifest 与 State        |
| `mr_boot_update_abort` | 放弃当前 RAM update context                       |

```c
mr_err_t mr_boot_update_begin(mr_boot_t *boot,
                              mr_boot_manifest_t *manifest);
mr_err_t mr_boot_update_write(mr_boot_t *boot, const void *data,
                              mr_size_t size);
mr_err_t mr_boot_update_end(mr_boot_t *boot);
void mr_boot_update_abort(mr_boot_t *boot);
```

成功的 `mr_boot_update_end()` 会使新 APP 成为 active slot。

#### 13.3 选择与启动

| API              | 说明                                                  |
|------------------|-------------------------------------------------------|
| `mr_boot_select` | 验证指定 slot，并准备 `boot->launch`；本身不执行 jump |
| `mr_boot_start`  | 按 `source()` 和 State 执行完整启动策略               |

```c
mr_err_t mr_boot_select(mr_boot_t *boot, mr_boot_slot_t slot);
mr_err_t mr_boot_start(mr_boot_t *boot);
```

`mr_boot_select()` 失败时会先清除旧 launch，避免上一次成功选择留下的地址被误用。update 进行期间 select/start 返回
`-MR_EBUSY`。

#### 13.4 SN

```c
mr_ssize_t mr_boot_sn_read(mr_boot_t *boot, void *data, mr_size_t size);
mr_ssize_t mr_boot_sn_write(mr_boot_t *boot, const void *data,
                            mr_size_t size);
```

返回实际读写字节数；负数为 backend 错误码。

### 14. Backend 回调契约

| 回调                    | 契约                                                                |
|-------------------------|---------------------------------------------------------------------|
| `read`                  | 返回实际读取字节数；Boot 要求请求必须完整完成，短读转换为 `-MR_EIO` |
| `program`               | 返回实际编程字节数；短写转换为 `-MR_EIO`                            |
| `erase`                 | 成功返回 0，失败返回负错误码                                        |
| `hash_begin/update/end` | 一个连续 SHA-256 session；Boot 会在不同验证阶段重新 begin           |
| `verify`                | 根据 `key_id` 验证传入的 32-byte digest 与 64-byte signature        |
| `auth`                  | 对传入 State 字节计算 32-byte 认证标签                              |
| `source`                | 返回当前启动来源                                                    |
| `rollback_read/update`  | 可选硬件防回滚计数器，成对使用                                      |
| `jump`                  | 跳到 firmware image 起始地址；成功启动时应不返回                    |
| `sn_read/write`         | 可选 SN 后端，语义由平台定义                                        |

错误码原样向上传递；Boot 只对“实际长度 != 请求长度”的 read/program 统一转换为
`-MR_EIO`。

### 15. 常见问题（FAQ）

**为什么 manifest 放在 slot 最前面，而不是最后？**

启动时只需先读固定大小 manifest 即可获得 image size、hash、签名和版本。受控更新时虽然 manifest 位于最前，但只有 image
完整验证后才会 program，因此半写入 image 不会提前形成 一个已提交的新版本。

**为什么 update end 还要把 image 从 Flash 再读一遍？**

流式 hash 只能证明“调用者传给 `mr_boot_update_write()` 的字节正确”。重新读取才能检查
`program()` 返回成功但实际存储发生位错误、总线异常或后端实现问题的情况。

**为什么 Recovery 可以低于 APP security_version？**

当前实现把 Recovery 视为独立恢复域，不受 APP 防回滚下限约束。相应地，Recovery 区域和其 签名密钥必须按平台威胁模型进行保护，不能把它当作普通第三个
APP slot。

**为什么 Rescue 不直接调用一个 rescue firmware？**

Rescue 的定义就是“留在 Boot 环境”。这样平台可以在所有可启动镜像都失败时仍保留最小 本地恢复能力，而不再依赖另一份 firmware。

**FAST 开启后还安全吗？**

FAST 不是降低验证标准，而是信任“此前已经完整验证过且 manifest 未变化”这一事实。这个 事实存放在 State 中，所以必须保证 State
可信；需要防篡改时应同时启用 AUTH 或使用等价硬件保护。

---

## 开发者指南

### 16. 架构总览

Boot Core 可以分成五层职责：

```text
                 mr_boot_start()
                       |
                Boot policy layer
                       |
              select / update / state
                 /       |       \
          verifier    storage    state
              \          |         /
                 mr_boot_ops_t
                       |
                    platform
```

- **Policy**：按 POWER_ON、WATCHDOG、UPDATE、ROLLBACK 等来源决定尝试顺序。
- **Verifier**：product、size、security_version、FAST、ECDSA、SHA-256。
- **Update**：inactive slot 擦除、顺序 program、双重 image hash、manifest/state 提交。
- **State**：双副本选择、sequence、invalid、active、安全版本、AUTH。
- **Backend**：所有硬件相关能力统一收敛到 `mr_boot_ops_t`。

Core 不知道实际 CPU 如何设置 MSP/VTOR，也不知道 Flash 擦写粒度；这些约束由 backend 实现和 `config` 布局负责。

### 17. 核心数据结构

#### `mr_boot_t`

```text
mr_object_t
config
state
update
launch
verify        # SHA256 enabled
ops
priv
```

`config` 是初始化时复制的固定布局；`state` 是当前加载的持久化状态；`update` 与
`launch` 是一次运行期事务上下文。

#### `mr_boot_state_t`

State 是 Boot 唯一主动持久化的可变控制记录。APP slot 内只有 manifest 与 image，不存在 额外的 Trial/Pending/Confirm
metadata。

`sequence` 使用半空间比较处理 32-bit 回绕：

```text
0 < (a - b) < 0x80000000  => a 比 b 新
```

这使 sequence 从 `0xffffffff` 回到 `0` 后仍能继续判定新旧。

### 18. 启动决策流程

#### POWER_ON / RESTART

```text
select(active)
  | success -> jump(active)
  | jump returns -> invalidate(active)
  v
select(other)
  | success -> activate(other) -> jump(other)
  | jump returns -> invalidate(other)
  v
Recovery -> Rescue
```

如果 active 只是验证失败而没有执行过 jump，Boot 不会先把它标 invalid；但如果 fallback 的 other 成功启动，它会成为新的
active。

#### WATCHDOG

```text
invalidate(active)
       |
       v
select(other) -> activate(other) -> jump
       |
       v
Recovery -> Rescue
```

WATCHDOG 的语义本身就是对 active APP 的负面判定，因此无需再次验证 active。

#### UPDATE

```text
select(inactive staged)
    | success -> activate(staged) -> jump
    |
    | EINVAL / EPERM
    +-> invalidate(staged)
    |
    v
select(old active) -> jump
    |
    v
fallback / Recovery / Rescue
```

只有确定表示“镜像本身不可接受”的错误才持久化 invalid，避免把暂时性的存储或密码后端故障 变成永久判废。

#### ROLLBACK

ROLLBACK 先验证另一 APP，成功后把旧 active 标 invalid、把另一 APP 设为 active，再执行 jump。若 jump 返回，新 APP 也会进入
fallback 失败处理。

### 19. 更新事务与掉电边界

受控更新的持久化顺序是：

```text
erase inactive slot
        |
program image chunks
        |
verify streaming hash
        |
re-read + verify stored image
        |
program manifest
        |
save State A
        |
save State B
```

关键性质：

- manifest 在 image 完整验证前不存在，因此 image 半写不会被当作有效新版本。
- State 在 manifest 之后提交，因此 manifest 写入但 State 尚未提交时，旧 active 仍是启动基线。
- State A 成功而 State B 失败时，下次 init 仍可从 A 恢复。
- State B 是冗余副本，不是第二阶段确认位。
- `boot_state_save()` 在 A 成功后会更新 RAM State，因此若随后 B 写失败，调用返回错误但当前 RAM 中已经是新
  State；重启后仍由持久化副本决定最终状态。

最后一点属于当前事务实现的重要边界，修改 State 提交流程时应保持测试覆盖。

### 20. 验证链

普通 APP 的完整验证顺序：

```text
manifest read
    |
product / image_size
    |
invalid flag
    |
security_version
    |
FAST manifest digest hit? -- yes --> accept
    |
   no
    v
ECDSA manifest signature
    |
SHA256 stored image
    |
accept
```

编译开关关闭时，对应步骤被裁掉。

受控 update 的 manifest 签名在 erase 前验证；image hash 则在数据全部写完后验证，并额外从 存储重算一次。

### 21. FAST 缓存

FAST cache 位于 State：

```c
mr_uint8_t manifest_digest[MR_BOOT_SLOT_MAX][MR_BOOT_SHA256_SIZE];
```

受控 A/B update 成功后，只更新新 active APP 对应的 digest。Recovery 没有公开的受控 update 流程，因此通常不会得到由 Boot 写入的
Recovery cache。

FAST 比较的是完整 manifest，包括 signature。只要任一 manifest 字节变化，就回退到完整 ECDSA + image hash 验证。

### 22. 并发与中断安全

Boot Core 内部没有锁，也不设计为多个线程同时驱动同一个 `mr_boot_t`。

约束：

- 对同一个对象的 init/update/select/start/SN 操作应由上层串行化。
- update session 进行期间 select/start 会返回 `-MR_EBUSY`。
- ops callback 在调用 Boot API 的同一执行上下文中同步执行。
- 不应在中断中直接执行大容量 hash、erase 或 program，除非平台明确保证这些 callback 可在中断上下文安全工作。
- `ops` 与 `priv` 生命周期必须覆盖 Boot 对象最终 release。
- 如果对象可能被其他组件持有，使用 `mr_boot_get()/mr_boot_put()` 管理引用。

### 23. 扩展与移植

平台移植通常只需要实现 `mr_boot_ops_t`，不需要修改 Boot Core。

推荐分层：

```text
Boot Core
  |
  +-- storage adapter  -> internal flash / external NOR / custom storage
  +-- crypto adapter   -> mrX Crypto / hardware crypto / secure service
  +-- reset adapter    -> reset flags + trusted software request
  +-- rollback adapter -> OTP / monotonic counter / secure element
  +-- jump adapter     -> architecture-specific image entry
```

`source()` 应只把可信来源映射为 `MR_BOOT_SOURCE_UPDATE`、ROLLBACK 或 RESCUE。若普通 APP 可以任意伪造这些来源，就会改变启动状态机的安全边界。

实现 `jump()` 时，Boot 传入的是：

```text
slot_offset + sizeof(mr_boot_manifest_t)
```

也就是 image 首字节地址，而不是 slot base。

### 24. 源码导读

| 文件 / 入口                                           | 说明                                                       |
|-------------------------------------------------------|------------------------------------------------------------|
| `include/mrX/modules/boot/mr_boot.h`                  | 公开结构、ops、API                                         |
| `modules/boot/boot.c::boot_config_check`              | 区域范围和重叠检查                                         |
| `modules/boot/boot.c::boot_state_load`                | 双 State 副本加载与 rollback sync                          |
| `modules/boot/boot.c::boot_state_save`                | State A/B 提交                                             |
| `modules/boot/boot.c::mr_boot_update_begin/write/end` | 受控升级事务                                               |
| `modules/boot/boot.c::mr_boot_select`                 | 单 slot 完整验证                                           |
| `modules/boot/boot.c::mr_boot_start`                  | 启动来源总入口                                             |
| `modules/boot/boot.c::boot_boot_installed`            | POWER_ON / RESTART                                         |
| `modules/boot/boot.c::boot_boot_watchdog`             | WATCHDOG                                                   |
| `modules/boot/boot.c::boot_boot_update`               | 外部 staged update                                         |
| `modules/boot/boot.c::boot_enter_recovery`            | Recovery / Rescue                                          |
| `test/modules/boot/boot.c`                            | State、掉电、fallback、crypto、FAST、Recovery 的系统级自测 |

### 25. 当前测试覆盖重点

Boot 自测不只检查 API 参数，还模拟真实故障和生命周期：

- A/B update 与反复切换。
- 最后一包刚好写满、越界、abort、短 program。
- `program()` 返回成功但实际存储字节损坏。
- manifest/state 提交各阶段失败与重新初始化恢复。
- State 双副本、sequence 回绕、单副本损坏。
- WATCHDOG、UPDATE、ROLLBACK、Recovery、Rescue。
- APP `jump()` 返回后的 invalid/fallback。
- SHA-256 image integrity。
- ECDSA manifest signature。
- AUTH State 篡改。
- FAST cache hit / miss。
- 有无硬件 rollback counter。
- SN backend 透传。

修改 Boot 的启动策略、持久化顺序或安全验证链时，应同步调整并保留这些回归场景。
