# 内核服务（Kernel）

内核服务提供 mrX 的基础运行能力，包括时钟基准、软件定时器、中断管理、等待/工作队列、对象系统与常用容器。可配置模块通过
`MR_USE_*` 开关裁剪，基础容器与工具宏始终编译。

## 模块列表

| 模块                | 说明                                                         | 文档                           |
|---------------------|--------------------------------------------------------------|--------------------------------|
| 初始化调用 Initcall | 通过链接段收集初始化函数，由 `mr_user_init()` 按级别依次执行 | [initcall.md](initcall.md)     |
| 入口 Entry          | 在 `main()` 之前完成内核初始化的工具链入口封装               | [entry.md](entry.md)           |
| 时钟 Clock          | 系统 tick / uptime 基准、同步 hook 与 tickless 剩余时间汇总  | [clock.md](clock.md)           |
| 软定时器 Timer      | 基于 tick 的单次/周期回调定时器                              | [timer.md](timer.md)           |
| 中断 Irq            | 中断控制器与 action 管理，支持可选 defer 工作队列            | [irq.md](irq.md)               |
| 等待队列 Wait       | 基于 `waitable`/`waiter` 的同步回调式事件通知                | [wait.md](wait.md)             |
| 工作队列 Work       | 立即或延时 work 排队与驱动执行                               | [work.md](work.md)             |
| 钩子 Hook           | 定长追加式回调表                                             | [hook.md](hook.md)             |
| 对象系统 Object     | 对象类型（clazz）注册、引用计数与生命周期管理                | [object.md](object.md)         |
| 块分配器 Slab       | 固定大小内存块对象池，使用原子位图管理                       | [slab.md](slab.md)             |
| 环形缓冲 FIFO       | 定长环形缓冲区，单生产者单消费者（SPSC）无锁安全             | [fifo.md](fifo.md)             |
| 哈希表 Hashmap      | 32 位整型键哈希表，节点内嵌无额外堆分配                      | [hashmap.md](hashmap.md)       |
| 日志 Log            | 带级别过滤和时间戳的日志输出                                 | [log.md](log.md)               |
| 跟踪 Trace          | 轻量级区间、事件与计数器内核跟踪接口                         | [trace.md](trace.md)           |
| 格式化输出 Printf   | 格式化输出并经由平台接口发送                                 | [printf.md](printf.md)         |
| 基础工具 Foundation | 侵入式链表、自旋锁、引用计数、常用工具宏与版本宏             | [foundation.md](foundation.md) |

## 相关文档

- 动态内存分配与字符串/格式化函数属于 C
  库层，见 [Malloc](../libc/malloc.md)、[Sprintf](../libc/sprintf.md)。
- 框架接入与编译配置见 [框架使用指南](../usage.md)。
- 平台相关的 tick、中断开关与输出接口见 [平台移植层](../platform/platform.md)。
