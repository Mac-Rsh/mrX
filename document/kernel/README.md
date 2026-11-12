# 内核服务

> 内核服务是 mrX 的核心层：时钟、中断、日志、对象系统、内存分配与各类调度原语。
> 每个模块的完整文档见下表；其中部分服务由 `MR_USE_*` 开关控制，基础容器/对象等模块则始终参与编译。

可配置模块只有在对应功能宏启用时才提供实现；无功能宏门控的基础模块始终可用。想确认具体编译条件、配置项与上下文约束，先看对应文档的“配置与依赖”和“并发与中断安全”章节。

## 模块索引

| 模块                | 文档                           | 一句话说明                                         |
|---------------------|--------------------------------|----------------------------------------------------|
| 初始化调用 Initcall | [initcall.md](initcall.md)     | 链接段收集初始化函数，由 `mr_user_init` 按级别执行 |
| 入口 Entry          | [entry.md](entry.md)           | 工具链入口接线：在 `main` 之前完成内核初始化       |
| 时钟 Clock          | [clock.md](clock.md)           | tick/uptime 基准与同步 hook、tickless 汇总         |
| 软定时器 Timer      | [timer.md](timer.md)           | 一次/周期到期回调，支持静态与动态对象              |
| 中断 Irq            | [irq.md](irq.md)               | IRQ 控制器/动作管理与可选 defer 工作队列           |
| 等待队列 Wait       | [wait.md](wait.md)             | waitable/waiter 的同步回调式事件分发               |
| 工作队列 Work       | [work.md](work.md)             | 立即/延时 work 排队，由调用者驱动 execute          |
| 钩子 Hook           | [hook.md](hook.md)             | 固定容量、追加式条目表；遍历由使用模块解释         |
| 对象系统 Object     | [object.md](object.md)         | 对象 clazz 注册、引用计数与生命周期                |
| 内存块分配器 Slab   | [slab.md](slab.md)             | 固定块对象池，原子位图管理分配状态                 |
| FIFO                | [fifo.md](fifo.md)             | 定长环形缓冲；普通读写支持 SPSC 使用模型           |
| 哈希表 Hashmap      | [hashmap.md](hashmap.md)       | 以 32 位整数为键的键值容器，节点内嵌无额外分配     |
| 日志 Log            | [log.md](log.md)               | 带级别和时间戳的日志输出                           |
| 格式化输出 Printf   | [printf.md](printf.md)         | 格式化字符串并经由平台输出接口送出                 |
| 基础工具 Foundation | [foundation.md](foundation.md) | 链表、引用计数、自旋锁、工具宏与版本宏             |

## 相关文档

- 内存分配 [Malloc](../libc/malloc.md)
  、字符串与格式化 [Sprintf](../libc/sprintf.md) 属于 C
  库层，见 [文档总索引](../README.md) 的 C 库一节。
- 框架接入流程（Kconfig、HTC、注册段、启动）见 [框架使用指南](../usage.md)。
- 平台相关的
  tick、中断开关和输出接口实现见 [平台移植层](../platform/platform.md)。
