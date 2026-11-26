# 扩展事件总线（XBus）

面向嵌入式与裸机/RTOS 环境的载体无关、域隔离发布订阅事件总线。提供基于命名空间（相对路径 `/<domain>/<path>` 与绝对路径 `/...`）、位图 ACL 访问控制、`*`/`?`/`#` 通配符匹配、拉取式 `STATE`/`QUEUE` 双订阅模式、可选无锁 `notify` 通知回调以及 `dentry` 风格内联/动态混合存储。

| 项目   | 说明                                                                                                                                                                     |
|--------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 功能宏 | `MR_USE_MODULE_XBUS`（可选 `MR_USE_MODULE_XBUS_MULTIUSER`、`MR_USE_MODULE_XBUS_TRACE`）                                                                                  |
| 头文件 | `<mrX/modules/xbus/mr_xbus.h>`                                                                                                                                           |
| 依赖   | [对象系统](../../kernel/object.md)、[哈希表](../../kernel/hashmap.md)、[时钟](../../kernel/clock.md)、[自旋锁](../../kernel/spinlock.md)、[内存分配](../../libc/malloc.md) |
| 测试   | `test/modules/xbus/xbus.c`                                                                                                                                               |

## 配置

| 配置项                         | 类型 | 默认值 | 说明                                                                           |
|--------------------------------|------|--------|--------------------------------------------------------------------------------|
| `MR_USE_MODULE_XBUS`           | bool | n      | 启用 XBus 事件总线模块                                                         |
| `MR_USE_MODULE_XBUS_MULTIUSER` | bool | n      | 启用多域上下文钩子，由平台提供 `mr_xbus_domain_current()`；关闭时默认返回 `host` |
| `MR_CFG_XBUS_DOMAIN_MAX`       | int  | 32     | 最大注册域数量（含内置 `host` 域），范围 1~32；自动缩放 `mr_xbus_acl_t` 位宽    |
| `MR_CFG_XBUS_INLINE_NAME_LEN`  | int  | 16     | 域/主题/订阅内联短名称缓冲字节数，范围 0~128（0 表示禁用，超长自动转堆分配）   |
| `MR_CFG_XBUS_INLINE_DATA_LEN`  | int  | 16     | 主题 `STATE` 内联小负载缓冲字节数，范围 0~256（0 表示禁用，超长自动转堆分配）   |
| `MR_CFG_XBUS_TOPIC_BUCKETS`    | int  | 8      | 全局主题哈希表桶数，范围 4~256                                                 |
| `MR_USE_MODULE_XBUS_TRACE`     | bool | n      | 开启内核 Trace 事件打点（依赖 `MR_USE_TRACE`）                                 |

## 快速上手

### 1. 创建域、发布主题与快照读取

```c
#include <mrX/modules/xbus/mr_xbus.h>

static mr_xbus_domain_t sensor_dom;
static mr_xbus_topic_t temp_topic;

void xbus_pub_demo(void) {
    mr_xbus_topic_cfg_t tcfg = {
        .read_mask = MR_XBUS_ACL_ALL,
        .write_mask = MR_XBUS_ACL_NONE,
        .max_size = sizeof(mr_int32_t),
        .type_id = 1,
        .flags = MR_XBUS_TOPIC_FIXED,
    };
    mr_int32_t temp = 265;
    mr_int32_t val = 0;

    /* 注册域与主题（相对路径 "temp" 自动解析为 "/sensor/temp"） */
    mr_xbus_domain_init(&sensor_dom, "sensor");
    tcfg.write_mask = MR_XBUS_ACL_DOMAIN(&sensor_dom);
    mr_xbus_topic_init(&temp_topic, &sensor_dom, "temp", &tcfg);

    /* 发布状态 */
    mr_xbus_publish(&sensor_dom, &temp_topic, &temp, sizeof(temp));

    /* 无订阅一次性快照读取 */
    mr_xbus_topic_read(MR_NULL, &temp_topic, &val, sizeof(val), MR_NULL);
}
```

### 2. STATE 与 QUEUE 拉取订阅

```c
#include <mrX/modules/xbus/mr_xbus.h>

static void on_bus_notify(mr_xbus_sub_t *sub, void *param) {
    /* notify 在释放内部锁之后执行，可安全唤醒工作队列或直接拉取 */
    (void)sub;
    (void)param;
}

void xbus_sub_demo(mr_xbus_domain_t *app_dom) {
    mr_xbus_sub_cfg_t qcfg = {
        .notify = on_bus_notify,
        .param = MR_NULL,
        .queue_depth = 8,
        .msg_size = sizeof(mr_int32_t),
        .mode = MR_XBUS_SUB_QUEUE,
        .flags = MR_XBUS_SUB_OVERWRITE,
    };
    mr_xbus_meta_t meta;
    mr_int32_t val = 0;
    mr_xbus_sub_t *sub;

    /* 订阅 "/sensor/#" 下所有具备读权限的主题 */
    sub = mr_xbus_sub_create(app_dom, "/sensor/#", &qcfg);
    if (sub) {
        while (mr_xbus_sub_read(sub, &val, sizeof(val), &meta) >= 0) {
            /* 处理消息与 meta.stamp / meta.seq / meta.flags */
        }
        mr_xbus_sub_del(sub);
    }
}
```

## 核心机制与约束

```text
+-------------+   publish()   +--------------+   bind    +----------------+
| Publisher   | ------------> | Topic        | --------> | STATE Sub      | (合并覆盖/粘性快照)
| (Domain A)  |   write ACL   | (/dom/path)  |  read ACL | (dirty_list)   |
+-------------+               +--------------+ --------> +----------------+
                                                         | QUEUE Sub      | (环形队列/定长或变长)
                                                         | (ring slots)   |
                                                         +-------+--------+
                                                                 |
                                                      unlocked notify() -> sub_read()
```

### 域、命名空间与 ACL
- **内置 `host` 域**：系统初始化时自动创建 ID 为 `0` 的 `host` 域（`mr_xbus_domain_default()`），不可通过 `mr_xbus_domain_del()` 删除。
- **路径解析**：不以 `'/'` 开头的相对路径按调用域解析为 `/<domain>/<path>`；以 `'/'` 开头的绝对路径直接规范化存储。路径段不允许为空、`.`、`..`，普通主题路径不允许包含 `*`、`?`、`#`；订阅模式中的 `#` 仅允许作为末尾独立段。
- **默认 ACL 规则**：若配置中 `read_mask == 0` 且 `write_mask == 0`，则按前缀自动推导：
  - `/host/...`：`host` 可写，所有域可读（`MR_XBUS_ACL_ALL`）；
  - `/<domain>/...`：仅该域可读写；
  - 其它共享前缀（如 `/shared/...`）：所有域可读写。
- **域 ID 复用与重置**：调用 `mr_xbus_domain_reset()` 会解绑并清空该域拥有的所有订阅，同时将该域最后发布过的主题标记 `MR_XBUS_FLAG_STALE` 并通知剩余 `STATE` 订阅者。删除域时会自动清理各主题 ACL 中的旧域位，避免 ID 复用导致权限越界。

### STATE 与 QUEUE 模式及混合存储
- **`STATE` 模式**：仅保留每主题最新快照。多次未读更新自动合并，并通过 `meta->dropped` 与 `MR_XBUS_FLAG_DROPPED` 告知跳过次数；支持 `MR_XBUS_SUB_RETAIN`（新订阅自动获得已有快照）与 `MR_XBUS_SUB_NOTIFY_RETAIN`。
- **`QUEUE` 模式**：每个订阅拥有独立环形队列。`msg_size > 0` 时分配连续定长槽位池（超出 `msg_size` 会截断并置位 `MR_XBUS_FLAG_TRUNCATED`）；`msg_size == 0` 时按需为变长负载分配槽位缓冲。队列满时默认丢弃新消息，置位 `MR_XBUS_SUB_OVERWRITE` 则覆盖最旧槽位。
- **`dentry` 风格内联存储**：名称不超过 `MR_CFG_XBUS_INLINE_NAME_LEN - 1`、主题负载不超过 `MR_CFG_XBUS_INLINE_DATA_LEN` 时直接使用对象内置数组，零额外堆分配；超出后透明切换为动态内存。
- **并发与中断约束**：所有内部状态受自旋锁保护；内存分配/释放与 `notify()` 回调均在释放锁之后执行，支持在 `notify()` 中重入发布、读取或删除自身。若需在中断上下文零堆操作（不触发 `mr_malloc`/`mr_free`）调用 `mr_xbus_publish()`，需同时满足：主题已通过 `max_size` 预分配容量（或负载命中内联缓冲）、当前绑定订阅数不超过 4 个、不包含尚未扩容到位的变长 `QUEUE` 订阅（`msg_size == 0`），且避免让 `MR_XBUS_SUB_OVERWRITE` 队列覆盖持有已删除动态主题最后引用的旧槽位（释放引用可能触发 `mr_free()`）；同时由于发布路径会同步执行匹配的 `notify()` 回调，中断上下文使用时所有相关回调必须保持 ISR 安全且足够短小（推荐仅做置位或提交 `workqueue`）。

## API 速查

### 域管理

| API                                                                 | 说明                             |
|---------------------------------------------------------------------|----------------------------------|
| `mr_err_t mr_xbus_domain_init(mr_xbus_domain_t *domain, const char *name)` | 初始化并注册静态域               |
| `mr_xbus_domain_t *mr_xbus_domain_create(const char *name)`         | 动态创建并注册域                 |
| `mr_err_t mr_xbus_domain_del(mr_xbus_domain_t *domain)`             | 注销并删除域（`host` 不可删除）  |
| `mr_xbus_domain_t *mr_xbus_domain_find(const char *name)`           | 按名称查找域（需配对 `put`）     |
| `mr_xbus_domain_t *mr_xbus_domain_default(void)`                    | 获取内置默认 `host` 域           |
| `mr_xbus_domain_t *mr_xbus_domain_current(void)`                    | 获取当前调用上下文域             |
| `mr_err_t mr_xbus_domain_reset(mr_xbus_domain_t *domain)`           | 重置域订阅并将所属主题标为陈旧   |
| `MR_XBUS_DOMAIN_NAME(_domain)` / `MR_XBUS_DOMAIN_ID(_domain)`       | 获取域名称 / 域 ID               |
| `mr_xbus_domain_get(domain)` / `mr_xbus_domain_put(domain)`         | 获取 / 释放域对象引用            |

### 主题管理与发布读取

| API                                                                                                      | 说明                               |
|----------------------------------------------------------------------------------------------------------|------------------------------------|
| `mr_err_t mr_xbus_topic_init(mr_xbus_topic_t *topic, mr_xbus_domain_t *domain, const char *path, const mr_xbus_topic_cfg_t *cfg)` | 初始化并注册静态主题               |
| `mr_xbus_topic_t *mr_xbus_topic_create(mr_xbus_domain_t *domain, const char *path, const mr_xbus_topic_cfg_t *cfg)` | 动态创建并注册主题                 |
| `mr_err_t mr_xbus_topic_del(mr_xbus_topic_t *topic)`                                                     | 注销并删除主题                     |
| `mr_xbus_topic_t *mr_xbus_topic_find(mr_xbus_domain_t *domain, const char *path)`                        | 按路径查找主题（需配对 `put`）     |
| `mr_err_t mr_xbus_topic_acl_set(mr_xbus_topic_t *topic, mr_xbus_acl_t read_mask, mr_xbus_acl_t write_mask)` | 整体设置主题读写 ACL 位图          |
| `mr_err_t mr_xbus_topic_acl_grant(mr_xbus_topic_t *topic, mr_xbus_domain_t *domain, mr_bool_t read, mr_bool_t write)` | 为指定域授予读写权限               |
| `mr_err_t mr_xbus_topic_acl_revoke(mr_xbus_topic_t *topic, mr_xbus_domain_t *domain, mr_bool_t read, mr_bool_t write)` | 撤销指定域的读写权限               |
| `mr_err_t mr_xbus_publish(mr_xbus_domain_t *domain, mr_xbus_topic_t *topic, const void *data, mr_size_t len)` | 向主题发布消息                     |
| `mr_err_t mr_xbus_publish_path(mr_xbus_domain_t *domain, const char *path, const void *data, mr_size_t len)` | 按路径查找并发布消息               |
| `mr_ssize_t mr_xbus_topic_read(mr_xbus_domain_t *domain, mr_xbus_topic_t *topic, void *buf, mr_size_t size, mr_xbus_meta_t *meta)` | 无订阅一次性快照读取主题最新状态   |
| `mr_err_t mr_xbus_topic_foreach(mr_xbus_topic_iter_t *iter, void *param)`                                | 遍历所有已注册主题                 |
| `MR_XBUS_TOPIC_NAME(_topic)`                                                                             | 获取规范化后的主题完整路径         |
| `mr_xbus_topic_get(topic)` / `mr_xbus_topic_put(topic)`                                                  | 获取 / 释放主题对象引用            |

### 订阅管理与数据拉取

| API                                                                                                      | 说明                               |
|----------------------------------------------------------------------------------------------------------|------------------------------------|
| `mr_err_t mr_xbus_sub_init(mr_xbus_sub_t *sub, mr_xbus_domain_t *domain, const char *pattern, const mr_xbus_sub_cfg_t *cfg)` | 初始化静态订阅                     |
| `mr_xbus_sub_t *mr_xbus_sub_create(mr_xbus_domain_t *domain, const char *pattern, const mr_xbus_sub_cfg_t *cfg)` | 动态创建订阅                       |
| `mr_err_t mr_xbus_sub_del(mr_xbus_sub_t *sub)`                                                           | 退订并删除订阅                     |
| `mr_ssize_t mr_xbus_sub_read(mr_xbus_sub_t *sub, void *buf, mr_size_t size, mr_xbus_meta_t *meta)`       | 拉取并消费下一条未读消息           |
| `mr_ssize_t mr_xbus_sub_peek(mr_xbus_sub_t *sub, void *buf, mr_size_t size, mr_xbus_meta_t *meta)`       | 查看下一条未读消息（不消费）       |
| `mr_uint32_t mr_xbus_sub_pending(mr_xbus_sub_t *sub)`                                                    | 查询待读取消息数                   |
| `mr_err_t mr_xbus_sub_clear(mr_xbus_sub_t *sub)`                                                         | 清空未读消息                       |
| `MR_XBUS_SUB_PATTERN(_sub)`                                                                              | 获取规范化后的订阅模式字符串       |
| `mr_xbus_sub_get(sub)` / `mr_xbus_sub_put(sub)`                                                          | 获取 / 释放订阅对象引用            |
