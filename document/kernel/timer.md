# 软定时器（Timer）

基于系统 tick 的软件定时器，支持单次与周期回调、静态初始化与动态创建，作为时钟钩子运行，并向时钟层返回距下次到期的剩余
tick 数。

| 项目   | 说明                                              |
|--------|---------------------------------------------------|
| 功能宏 | `MR_USE_TIMER`（`MR_USE_WORK` 会自动选中）        |
| 头文件 | `<mrX/kernel/mr_timer.h>`                         |
| 依赖   | [对象系统](object.md)、[时钟](clock.md)           |
| 测试   | `test/kernel/timer.c`、`test/kernel/timer_deep.c` |

## 配置

| 配置项                   | 类型 | 默认值 | 说明                                                   |
|--------------------------|------|--------|--------------------------------------------------------|
| `MR_USE_TIMER`           | bool | y      | 启用软定时器模块                                       |
| `MR_CFG_TICK_PER_SECOND` | int  | 1000   | 系统 tick 频率（Hz），决定定时最小粒度（时钟模块配置） |

## 快速上手

```c
#include <mrX/kernel/mr_timer.h>

static void led_timer_cb(mr_timer_t *timer, void *param) {
    /* 每 500 ms 在此执行（tick 上下文，禁止阻塞） */
}

void timer_demo(void) {
    static mr_timer_t led_timer;

    /* 周期模式：tick 按位或 MR_TIMER_PERIODIC */
    mr_timer_init(&led_timer, led_timer_cb, MR_NULL,
                  MR_TICK_MSEC(500) | MR_TIMER_PERIODIC);
    mr_timer_start(&led_timer);
}
```

## 核心机制与约束

- **回调执行上下文**：到期检查函数 `timer_timeout_check()` 在
  `mr_kernel_timer_init()` 中注册为时钟钩子，因此在调用 `mr_clock_increase()`
  的上下文中同步执行（通常为 tick
  中断）。回调应保持短小且严禁阻塞；耗时或需睡眠的逻辑应投递到 [工作队列](work.md)。
- **单次与周期模式**：`tick` 参数按位或 `MR_TIMER_PERIODIC`（bit
  31）为周期模式，否则为单次模式。去掉标志位后的有效 tick 数必须大于 0（否则触发
  `MR_ASSERT`）。周期定时器在回调返回后以 **当前 `mr_clock_tick()`**
  为基准重新插入，因此回调耗时会导致相位顺延。
- **回调内自操作**：回调执行期间，定时器被临时移出全局计时链表，因此回调内可安全调用
  `mr_timer_start()`（自重启）、`mr_timer_stop()`、`mr_timer_tick_set()` 或
  `mr_timer_del()`（自销毁）。若回调已对自身执行上述操作，框架不会再为其自动重装。
- **运行中重操作**：对正在计时的定时器调用 `mr_timer_start()` 会从当前时刻重新计时；调用
  `mr_timer_tick_set()` 会修改周期并自动停止定时器，需重新 `mr_timer_start()`。
- **存储生命周期**：静态初始化的 `mr_timer_t`
  在启动后会被全局链表异步引用，不得使用会离开作用域的栈变量；使用完毕后调用
  `mr_timer_del()` 摘除并归还引用。

## API 速查

### 初始化与销毁

| API                                                           | 说明                                                           |
|---------------------------------------------------------------|----------------------------------------------------------------|
| `MR_TIMER_INIT(_timer, _entry, _param, _tick)`                | 静态初始化定时器描述符（静态释放通道）                         |
| `MR_TIMER_DERIVE_INIT(_timer, _clazz, _entry, _param, _tick)` | 静态初始化派生定时器（指定自定义 `clazz`）                     |
| `mr_err_t mr_timer_init(timer, entry, param, tick)`           | 运行时初始化静态定时器，成功返回 `0`                           |
| `mr_timer_t *mr_timer_create(entry, param, tick)`             | 堆上动态创建定时器，失败返回 `MR_NULL`                         |
| `mr_err_t mr_timer_del(timer)`                                | 从计时链表摘除并归还一次对象引用（动态对象引用归零时释放内存） |

### 控制与状态

| API                                                                 | 说明                                                    |
|---------------------------------------------------------------------|---------------------------------------------------------|
| `mr_err_t mr_timer_start(timer)`                                    | 启动定时器；若已在计时则从当前时刻重装                  |
| `mr_err_t mr_timer_stop(timer)`                                     | 停止定时器（对未启动定时器安全无操作）                  |
| `MR_TIMER_IS_TIMING(_timer)`                                        | 判断定时器是否正在计时链表中                            |
| `mr_err_t mr_timer_tick_set(timer, tick)`                           | 修改定时周期（若正在计时会被停止）                      |
| `mr_tick_t mr_timer_tick(timer)`                                    | 读取当前配置的 `itick`（含 `MR_TIMER_PERIODIC` 标志位） |
| `mr_timer_t *mr_timer_get(timer)` / `mr_bool_t mr_timer_put(timer)` | 增减对象引用计数                                        |

## 内部实现

`kernel/timer.c` 维护一条按到期时间 `ttick` 升序排列的全局双向链表
`__timer_list`，由自旋锁 `__timer_lock`（`mr_spinlock_lock_irqsave`）保护。插入和到期比较均采用
`(a - b) < (MR_TICK_MAX / 2)` 处理 tick 溢出回绕。`timer_timeout_check()`
每次只从链表头处理已到期项，并在解锁后执行回调（期间定时器暂存于局部链表 `oplist`
），最后返回距离表头下一项的剩余 tick 数供 tickless 调度使用。
