# 启动与固件更新（Boot）

面向嵌入式设备的 A/B 启动、受控固件更新、回退、Recovery 与 Rescue
策略层。存储访问、复位来源、跳转、Hash、验签、State 认证与防回滚计数器均由平台通过
`mr_boot_ops_t` 注入，Boot Core 不绑定具体 Flash、CPU 或密码硬件。

| 项目   | 说明                                                                                                                                      |
|--------|-------------------------------------------------------------------------------------------------------------------------------------------|
| 功能宏 | `MR_USE_MODULE_BOOT`（可选子功能见配置表）                                                                                                |
| 头文件 | `<mrX/modules/boot/mr_boot.h>`                                                                                                            |
| 依赖   | [对象系统](../../kernel/object.md)、[内存分配](../../libc/malloc.md)（仅 `mr_boot_create`）；密码后端可选用 [Crypto](../crypto/crypto.md) |
| 测试   | `test/modules/boot/boot.c`                                                                                                                |

## 配置

| 配置项                           | 类型 | 默认值 | 说明                                                    |
|----------------------------------|------|--------|---------------------------------------------------------|
| `MR_USE_MODULE_BOOT`             | bool | n      | 启用 Boot 模块                                          |
| `MR_USE_MODULE_BOOT_SHA256`      | bool | y      | 对 firmware image 做 SHA-256 完整性校验                 |
| `MR_USE_MODULE_BOOT_ECDSA_P256`  | bool | y      | 对 manifest 做 ECDSA P-256 验签，自动选中 SHA-256       |
| `MR_USE_MODULE_BOOT_RECOVERY`    | bool | n      | 启用独立 Recovery firmware 区域                         |
| `MR_USE_MODULE_BOOT_AUTH`        | bool | y      | 对持久化 State 做认证（如 HMAC-SHA256）                 |
| `MR_USE_MODULE_BOOT_FAST`        | bool | n      | manifest 摘要未变化时跳过完整 image/hash/signature 校验 |
| `MR_CFG_BOOT_VERIFY_BUFFER_SIZE` | int  | 256    | 重算存储中 image hash 的分块缓冲字节数，范围 64~4096    |

## 快速上手

启动入口只需注入平台存储、来源与跳转回调（未启用的功能无需提供空实现）：

```c
#include <mrX/modules/boot/mr_boot.h>

static mr_boot_t boot;

static const mr_boot_config_t boot_config = {
    .product_id = 0x12345678U,
    .slot_a_offset = 0x00100000ULL,
    .slot_a_size = 0x00080000ULL,
    .slot_b_offset = 0x00180000ULL,
    .slot_b_size = 0x00080000ULL,
#if defined(MR_USE_MODULE_BOOT_RECOVERY)
    .recovery_offset = 0x00200000ULL,
    .recovery_size = 0x00080000ULL,
#endif
    .state_a_offset = 0x00280000ULL,
    .state_b_offset = 0x00281000ULL,
    .state_size = 0x00001000ULL,
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

mr_err_t boot_main(void) {
    mr_err_t ret = mr_boot_init(&boot, &boot_config, &boot_ops, MR_NULL);
    if (ret != 0) {
        return ret;
    }
    /* 成功后 jump() 不应返回；进入 Rescue 时返回 0 且 boot.launch.slot == MR_BOOT_SLOT_MAX */
    return mr_boot_start(&boot);
}
```

受控更新始终写入当前的 **inactive APP slot**，提交后自动切换为新的 active slot：

```c
mr_err_t update_image(mr_boot_t *boot, mr_boot_manifest_t *manifest,
                      const void *image, mr_size_t size) {
    mr_err_t ret = mr_boot_update_begin(boot, manifest);
    if (ret != 0) {
        return ret;
    }

    ret = mr_boot_update_write(boot, image, size);
    if (ret == 0) {
        ret = mr_boot_update_end(boot);
    }
    if (ret != 0) {
        mr_boot_update_abort(boot);
    }
    return ret;
}
```

## 核心机制与约束

### 存储布局与 Manifest

每个 APP / Recovery slot 均由固定头部 `mr_boot_manifest_t` 紧跟 firmware image
组成，State 则使用两份独立区域冗余保存同一份 `mr_boot_state_t`：

```text
APP A / B / Recovery Slot:               State Copy A / B:
+----------------------------+           +----------------------------+
| mr_boot_manifest_t         |           | mr_boot_state_t            |
|  - product_id              |           |  - sequence (32-bit)       |
|  - firmware_version        |           |  - security_version        |
|  - security_version        |           |  - manifest_digest[] (FAST)|
|  - image_size              |           |  - invalid[]               |
|  - key_id                  |           |  - active_slot (A or B)    |
|  - image_hash[32]          |           |  - auth[32] (AUTH)         |
|  - signature[64]           |           +----------------------------+
+----------------------------+
| firmware image             |
| (image_size bytes)         |
+----------------------------+
```

- **区域约束**：A、B、Recovery、State A、State B 之间不得重叠；slot 容量至少容纳
  `sizeof(mr_boot_manifest_t)`，State 区至少容纳 `sizeof(mr_boot_state_t)`。Boot
  不假设擦除后字节为 `0xFF`，有效性完全由结构与认证结果判定。
- **签名范围**：开启 ECDSA P-256 时，签名输入为
  `SHA256(manifest 从首字节到 signature 字段之前的全部字节)`。`jump()` 传入的入口偏移为
  `slot_offset + sizeof(mr_boot_manifest_t)`（image 首字节）。

### 启动策略与回退链

`mr_boot_start()` 根据 `ops->source()` 返回的启动源决定尝试顺序：

| 启动源                                | 策略与行为                                                                                                  |
|---------------------------------------|-------------------------------------------------------------------------------------------------------------|
| `MR_BOOT_SOURCE_POWER_ON` / `RESTART` | 验证并启动 active APP → 失败则尝试另一 APP → Recovery → Rescue                                              |
| `MR_BOOT_SOURCE_WATCHDOG`             | 先将当前 active APP 持久化标为 invalid，再尝试另一 APP → Recovery → Rescue                                  |
| `MR_BOOT_SOURCE_UPDATE`               | 验证 inactive APP；成功则切换 active 并启动；明确无效（`-MR_EINVAL`/`-MR_EPERM`）则标废该 slot 并继续旧 APP |
| `MR_BOOT_SOURCE_ROLLBACK`             | 验证另一 APP，成功后标废旧 active、切换 active 并启动；失败走回退链                                         |
| `MR_BOOT_SOURCE_RECOVERY`             | 直接验证并启动 Recovery；未启用 Recovery 时返回 `-MR_ENOSYS`                                                |
| `MR_BOOT_SOURCE_RESCUE`               | 不执行跳转，清空 `boot->launch` 并返回 `0`                                                                  |

```mermaid
flowchart LR
    A["active APP"] -->|验证失败或 jump 返回| B["other APP"]
    B -->|验证失败或 jump 返回| C["Recovery（可选）"]
    C -->|不可用或 jump 返回| D["Rescue（留在 Boot）"]
```

- 成功的 `jump()` 不应返回；若返回，Boot 将其视为启动失败，将该 APP 标为 invalid
  并继续回退。
- **Recovery** 是独立恢复域：不检查 A/B 的 invalid 标志、不受 APP
  `security_version` 下限限制、不改写 A/B State，但仍校验 `product_id`、
  `image_size` 及已启用的签名/Hash。
- **Rescue** 不是固件镜像，进入后 `boot->launch.slot == MR_BOOT_SLOT_MAX` 且
  `mr_boot_start()` 返回 `0`，供上层进入串口刷机或本地恢复模式。

### 受控更新事务与掉电边界

受控更新按严格顺序推进，确保中途掉电不会提交半成品镜像：

1. `mr_boot_update_begin()`：校验 `product_id`、`image_size` 与 `security_version`
   ；若开启 ECDSA 则 **先验签再擦除**，签名无效不会擦除目标 slot；随后擦除整个
   inactive slot 并开启流式 Hash。
2. `mr_boot_update_write()`：仅支持顺序追加，不允许写越 `image_size`；每段先
   `program` 再更新流式 Hash，底层短写统一转换为 `-MR_EIO`。
3. `mr_boot_update_end()`：确认写入总长等于 `image_size`，比对流式 Hash；随后
   **从存储重新读取完整 image** 做二次 Hash 比对（防止 `program` 静默写坏）；校验全部通过后先写入
   manifest，再依次写入 State A 与 State B（将新 slot 设为 active 并清除其 invalid
   标志）。
4. `mr_boot_update_abort()`：仅清除 RAM 中的更新上下文，不额外擦除存储。

### 防回滚、AUTH 与 FAST 安全边界

- **防回滚**：APP 的 `security_version` 不得低于当前安全下限（否则返回 `-MR_EPERM`
  ）。若平台同时提供 `rollback_read` 和 `rollback_update`（必须成对提供），运行时以硬件计数器为准；仅当
  AUTH 开启且加载到已认证 State 时，才允许用更高的 State `security_version`
  推进硬件计数器。
- **AUTH**：`ops->auth()` 对 `mr_boot_state_t` 中 `auth` 字段之前的全部字节计算
  32 字节认证标签，密钥由平台侧管理。AUTH 不替代 image SHA-256 或 manifest 验签。
- **FAST**：受控更新成功后将完整 manifest 的 SHA-256 缓存到 State 中；启动时若存储中的
  manifest 摘要命中缓存，则跳过 ECDSA 与全量 image Hash。 **开启 FAST 必须同时启用
  AUTH 或具备硬件 State 防篡改保护**，否则伪造 State 缓存将绕过验签。

### 并发与生命周期约束

- Boot Core 内部无锁，对同一个 `mr_boot_t` 的所有 API 调用须由上层串行化；update
  会话期间调用 `select`/`start` 返回 `-MR_EBUSY`。
- `config` 在初始化时复制到对象内部；`ops` 与 `priv` 仅保存指针，生命周期必须覆盖
  Boot 对象的最终 release。
- `ops` 回调在调用者上下文同步执行，不应在中断上下文中直接触发大块 Hash、擦除或编程。

## API 速查

### 生命周期与业务接口

| 分组     | API                                       | 说明                                               |
|----------|-------------------------------------------|----------------------------------------------------|
| 生命周期 | `mr_boot_init(boot, config, ops, priv)`   | 初始化静态对象并加载 State                         |
| 生命周期 | `mr_boot_create(config, ops, priv)`       | 动态分配并初始化对象                               |
| 生命周期 | `mr_boot_del(boot)`                       | 归还对象初始引用                                   |
| 生命周期 | `mr_boot_get(boot)` / `mr_boot_put(boot)` | 增加 / 释放对象引用计数                            |
| 受控更新 | `mr_boot_update_begin(boot, manifest)`    | 校验 manifest、验签、擦除 inactive slot 并启动会话 |
| 受控更新 | `mr_boot_update_write(boot, data, size)`  | 顺序写入下一段 image 数据                          |
| 受控更新 | `mr_boot_update_end(boot)`                | 双重 Hash 校验并提交 manifest 与双副本 State       |
| 受控更新 | `mr_boot_update_abort(boot)`              | 放弃当前 RAM 更新上下文                            |
| 启动控制 | `mr_boot_select(boot, slot)`              | 验证指定 slot 并填充 `boot->launch`（不执行跳转）  |
| 启动控制 | `mr_boot_start(boot)`                     | 按 `ops->source()` 和 State 执行完整启动策略       |
| 序列号   | `mr_boot_sn_read(boot, data, size)`       | 透传 `ops->sn_read`，未提供返回 `-MR_ENOSYS`       |
| 序列号   | `mr_boot_sn_write(boot, data, size)`      | 透传 `ops->sn_write`，未提供返回 `-MR_ENOSYS`      |

### `mr_boot_ops_t` 后端契约

| 回调                                      | 必需性                       | 契约说明                                                           |
|-------------------------------------------|------------------------------|--------------------------------------------------------------------|
| `read` / `program`                        | 必需（`program` 仅更新时用） | 返回实际读写字节数；长度不等于请求长度时 Boot 统一转换为 `-MR_EIO` |
| `erase`                                   | 更新时必需                   | 成功返回 0，失败返回负错误码                                       |
| `hash_begin` / `hash_update` / `hash_end` | 开启 SHA-256 时必需          | 连续流式 SHA-256 会话，Boot 会在不同阶段重新 `begin`               |
| `verify`                                  | 开启 ECDSA 时必需            | 根据 `key_id` 验证 32 字节摘要与 64 字节签名                       |
| `auth`                                    | 开启 AUTH 时必需             | 对 State 前导字节计算 32 字节认证标签                              |
| `source` / `jump`                         | 启动时必需                   | `source` 返回启动源；`jump` 传入 image 首字节偏移，成功不应返回    |
| `rollback_read` / `rollback_update`       | 可选（成对）                 | 硬件单调防回滚计数器读写，缺任一则回退为纯软件 State 版本          |
| `sn_read` / `sn_write`                    | 可选                         | 设备序列号读写，语义由平台定义                                     |

## 内部实现

- **State 双副本恢复**：`boot_state_load()`（位于 `modules/boot/boot.c`）初始化时同时读取
  State A 与 State B；两份均合法时使用半空间回绕比较 `0 < (a - b) < 0x80000000`
  选取较新的 `sequence`（相同时取后读副本）；无合法副本且无底层读取错误时从默认
  State（`active_slot = MR_BOOT_SLOT_A`，其余清零）启动；若存在真实 I/O
  错误且无合法副本则直接初始化失败。
- **State 提交**：`boot_state_save()` 递增 `sequence` 后先写 State A、再写 State
  B；State A 写入成功后即更新 RAM 中的 `boot->state`。
- **源码索引**：区域校验 `boot_config_check`、双副本加载与保存 `boot_state_load` /
  `boot_state_save`、策略分发 `mr_boot_start`（`boot_boot_installed` /
  `boot_boot_watchdog` / `boot_boot_update` / `boot_enter_recovery`）均位于
  `modules/boot/boot.c`。
