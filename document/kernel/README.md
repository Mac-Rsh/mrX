# 内核服务

> 内核服务是 mrX 的核心层：时钟、中断、日志、对象系统、内存分配与各类调度原语。
> 每个模块的完整文档见下表；大部分模块都是可选的，由 `MR_USE_*` 开关控制是否编译进来。

内核服务面向所有应用可用，但只有被启用的模块才参与编译。想确认某个模块是否已启用、
它有哪些配置项，先看它文档开头的元数据表和"配置与依赖"一节。

## 模块索引

| 模块                | 文档                       | 一句话说明                                     |
|---------------------|----------------------------|------------------------------------------------|
| 初始化调用 Initcall | [initcall.md](initcall.md) | 按级别注册启动函数，框架启动时统一执行         |
| 入口 Entry          | [entry.md](entry.md)       | 工具链入口接线：在 `main` 之前完成内核初始化   |
| 时钟 Clock          | [clock.md](clock.md)       | tick 与 uptime 基准，提供钩子和 tickless 声明  |
| 软定时器 Timer      | [timer.md](timer.md)       | 一次/周期到期回调，支持静态与动态对象          |
| 中断 Irq            | [irq.md](irq.md)           | 中断注册与顶半部/底半部分工                    |
| 等待队列 Wait       | [wait.md](wait.md)         | 等待/唤醒原语，支撑阻塞式接口                  |
| 工作队列 Work       | [work.md](work.md)         | 把工作推迟到队列上下文执行，支持延时           |
| 钩子 Hook           | [hook.md](hook.md)         | 通用钩子链，供模块挂接扩展点                   |
| 对象系统 Object     | [object.md](object.md)     | 对象 clazz 注册、引用计数与生命周期            |
| 内存块分配器 Slab   | [slab.md](slab.md)         | 固定块大小、零碎片、无锁的对象池               |
| FIFO                | [fifo.md](fifo.md)         | 定长环形缓冲，适合中断与线程之间的数据传递     |
| 哈希表 Hashmap      | [hashmap.md](hashmap.md)   | 以 32 位整数为键的键值容器，节点内嵌无额外分配 |
| 日志 Log            | [log.md](log.md)           | 带级别和时间戳的日志输出                       |
| 格式化输出 Printf   | [printf.md](printf.md)     | 格式化字符串并经由平台输出接口送出             |

## 相关文档

- 内存分配 [Malloc](../libc/malloc.md)、字符串与格式化 [Sprintf](../libc/sprintf.md) 属于 C
  库层，见 [文档总索引](../README.md) 的 C 库一节。
- 框架接入流程（Kconfig、HTC、注册段、启动）见 [框架使用指南](../usage.md)。
- 平台相关的 tick、中断开关和输出接口实现见 [平台移植层](../platform/platform.md)。
