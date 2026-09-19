# 测试框架（Test）

> 测试框架提供轻量级单元测试能力：`MR_TEST` 宏定义用例、链接器段自动收集用例、
> EXPECT/ASSERT 双系列断言宏校验结果，并输出 gtest 风格的运行报告。

| 项       | 说明                                                                              |
|----------|-----------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_TEST`（默认 n，自动选中 `MR_USE_PRINTF`）                                 |
| 头文件   | `<mrX/test/mr_test.h>`（断言与用例定义）、`<mrX/test/mr_check.h>`（底层比较实现） |
| 依赖模块 | [格式化输出](../kernel/printf.md)（结果输出）、链接器段收集（`mr_testcase.*` 段） |
| 参考用例 | `mrX/test/`（kernel/libc/drivers/fs/modules 各子系统）                            |

---

## 用户指南

### 1. 模块概述

测试框架解决的问题是： **让散落在各模块的行为验证可以自动收集、统一运行、 一键判定**。特性：

- 用 `MR_TEST(suite, name)` 定义用例，编译期经 `mr_testcase.*` 链接器段自动注册， 无需手工登记用例表。
- 断言宏覆盖 EQ/NE/LT/LE/GT/GE/NEAR 七种比较 × int/uint/ptr/float/double 五种 类型，并区分 EXPECT（失败继续）与
  ASSERT（失败中止）两种失败策略。
- 运行报告为 gtest 风格（`[ RUN ]` / `[ OK ]` / `[ FAILED ]` / 汇总统计）。
- 运行入口为公开 API（`mr_test_run` / `mr_testsuite_run` / `mr_testcase_run`），由应用自行调用，并可用返回值对接 CI。

适用边界：框架面向 x64 宿主模拟环境的功能验证（`./build/mrX.exe`），不在目标板 资源受限环境下使用；其依赖的 `mr_printf`
、链接器段机制均以宿主为第一场景。

### 2. 配置与依赖

| 配置项                | 类型     | 默认值        | 说明                                                                                                                                                                                      |
|-----------------------|----------|---------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `MR_USE_TEST`         | bool     | n             | 启用测试框架（`mrX/include/mrX/test/Kconfig`）；自动 `select MR_USE_PRINTF`                                                                                                               |
| `MR_USE_TEST_PTHREAD` | 编译定义 | 无 Kconfig 项 | 门控基于 pthread 的多线程并发用例（如 `mrX/test/kernel/fifo.c`、`mrX/test/fs/concurrency.c`）；由 mrX 上一级项目根的 `CMakeLists.txt` 与 `MR_USE_TEST` 一并定义（`CMakeLists.txt:78-81`） |

注意：项目根 `CMakeLists.txt`（位于 `mrX/` 的上一级）对宿主可执行文件无条件定义 `MR_USE_TEST` 与
`MR_USE_TEST_PTHREAD`，因此 `./build/mrX.exe` 始终编入测试，Kconfig 默认值 n 不影响宿主模拟构建。各用例文件除 `MR_USE_TEST`
外通常还按被测模块宏二次门控 （如 `mrX/test/kernel/timer.c` 整体包在 `#if defined(MR_USE_TIMER)` 内）。

启用后重新生成 `mr_config.h`，测试代码经 `#if defined(MR_USE_TEST)` 门控编入：

```c
#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
/* tests */
#endif /* defined(MR_USE_TEST) */
```

构建：`cmake -S . -B build && cmake --build build`。测试由应用显式驱动：默认宿主示例 `user/main.c`（位于 `mrX/` 的上一级）运行的是 DMA/RPC 演示，不调用测试入口；要执行测试需在应用启动流程中自行调用 `mr_test_run()`（全部通过返回 `MR_TRUE`），并据此决定进程退出码。

### 3. 快速上手

定义并自动运行一个用例（取自 `mrX/test/kernel/timer.c:55` 的模式）：

```c
#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)

MR_TEST(timer, init_del) {
    mr_timer_t timer;
    mr_err_t ret;

    ret = mr_timer_init(&timer, test_timer_entry, MR_NULL, 1000);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&timer), MR_TRUE);

    ret = mr_timer_del(&timer);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

#endif /* defined(MR_USE_TEST) */
```

编译运行后框架自动发现并执行该用例，输出：

```
[ RUN      ] timer.init_del
[       OK ] timer.init_del
[==========]
[  PASSED  ] N tests
[  FAILED  ] 0 tests
```

### 4. 使用说明

**用例定义**：`MR_TEST(suite, name)` 展开为一个静态函数 + 一个 `mr_testcase_t`
段变量（`mrX/include/mrX/test/mr_test.h:183`）。`suite` 按被测模块命名（如
`timer`、`malloc`），`name` 描述被测行为；同一 suite 内的用例在
`mr_testsuite_run` 时按链接顺序执行（同一文件内即声明顺序，跨文件按构建系统 收集顺序）。

**EXPECT 与 ASSERT**：

- `MR_TEST_EXPECT_*`：失败时打印实际值与期望值、将状态标记为失败， **用例继续 执行**，适合收集同一用例内的多个独立错误。
- `MR_TEST_ASSERT_*`：失败时打印并 **立即返回**，适合做前置条件检查——后续步骤 依赖该结果时（如初始化失败后继续调用会崩）必须用
  ASSERT。
- 惯用法：关键前置用 ASSERT，后续校验用 EXPECT，收尾清理用 `goto _exit` 跳转 （见 `mrX/test/kernel/timer.c:82` 的
  `start_stop` 用例）。

**浮点比较**：`*_EQ_FLOAT` / `*_EQ_DOUBLE` 内置容差（1e-4 / 1e-8）；自定义容差 用 `MR_TEST_NEAR(a, b, eps)`。

**断言宏选择**：类型后缀决定比较实现——`INT`（有符号）、`UINT`（无符号）、
`PTR`（指针）、`FLOAT`（float）、`DOUBLE`（double）。无符号量用 `EQ_INT` 比较 可能在负值路径上误判，按量纲选对后缀。

### 5. API 参考

用例定义：

| API                    | 说明                                         |
|------------------------|----------------------------------------------|
| `MR_TEST(suite, name)` | 定义并自动导出一个测试用例（函数体即用例体） |

运行入口（由应用自行调用）：

| API                            | 说明                                                        |
|--------------------------------|-------------------------------------------------------------|
| `mr_test_run`                  | 运行全部用例；全部通过返回 `MR_TRUE`                        |
| `mr_testsuite_run(suite)`      | 运行指定 suite 的全部用例；至少运行一个且全过返回 `MR_TRUE` |
| `mr_testcase_run(suite, name)` | 运行单个用例；存在且通过返回 `MR_TRUE`                      |

断言宏（`EXPECT` 失败继续 / `ASSERT` 失败返回，每种比较 × 类型各一对）：

| 比较 | int       | uint       | ptr       | float/double               | 自定义容差 |
|------|-----------|------------|-----------|----------------------------|------------|
| 相等 | `_EQ_INT` | `_EQ_UINT` | `_EQ_PTR` | `_EQ_FLOAT` / `_EQ_DOUBLE` | `_NEAR`    |
| 不等 | `_NE_INT` | `_NE_UINT` | `_NE_PTR` | `_NE_FLOAT` / `_NE_DOUBLE` | —          |
| 小于 | `_LT_INT` | `_LT_UINT` | `_LT_PTR` | `_LT_FLOAT` / `_LT_DOUBLE` | —          |
| ≤    | `_LE_*`   | 同型       | 同型      | 同型                       | —          |
| 大于 | `_GT_*`   | 同型       | 同型      | 同型                       | —          |
| ≥    | `_GE_*`   | 同型       | 同型      | 同型                       | —          |

前缀 `MR_TEST_` 省略。所有宏在 `MR_USE_TEST` 未定义时整体消失，零开销。

### 6. 完整示例

示例 1：EXPECT 收集多个错误 + ASSERT 前置保护（模式取自
`mrX/test/kernel/timer.c:82`）：

```c
MR_TEST(timer, start_stop) {
    mr_timer_t timer;
    test_timer_ctx_t ctx = {0};
    mr_err_t ret;
    int timer_inited = MR_FALSE;

    ret = mr_timer_init(&timer, test_timer_entry, &ctx, 2);
    MR_TEST_ASSERT_EQ_INT(ret, 0);          /* Hard precondition */
    timer_inited = MR_TRUE;

    ret = mr_timer_start(&timer);
    MR_TEST_EXPECT_EQ_INT(ret, 0);          /* Soft check, keep going */
    if (ret != 0) {
        goto _exit;
    }

    ctx.last_timer = MR_NULL;
    mr_clock_increase(2);
    MR_TEST_EXPECT_EQ_PTR(ctx.last_timer, &timer);

    ret = mr_timer_stop(&timer);
    MR_TEST_EXPECT_EQ_INT(ret, 0);

_exit:
    if (timer_inited == MR_TRUE) {
        mr_timer_del(&timer);               /* Cleanup on every path */
    }
}
```

示例 2：浮点与容差比较：

```c
MR_TEST(math, near) {
    mr_f64_t v = 0.1 + 0.2;

    MR_TEST_NEAR(v, 0.3, 1e-9);
    MR_TEST_EXPECT_EQ_DOUBLE(v, 0.3);       /* 1e-8 tolerance */
}
```

### 7. 常见问题（FAQ）

**Q：新写的用例没有被运行？**

确认文件经构建系统收集（`mrX/test/**/*.c` 已被根 CMake 覆盖）；确认
`MR_USE_TEST` 已启用且代码在 `#if defined(MR_USE_TEST)` 门控内；确认
`MR_TEST(suite, name)` 没有拼错（宏展开才是注册发生的地方）。

**Q：ASSERT 失败后清理代码没执行？**

ASSERT 的失败路径是直接 `return`（经状态魔数切换实现，见 `mr_check.h:101`）， 不会走用例尾部的清理逻辑。需要保证清理的用例把
ASSERT 放在最前，或改用 EXPECT + `goto _exit` 模式。

**Q：用例之间共享状态怎么办？**

每个用例是独立函数，框架不提供夹具（fixture）。跨用例复用通过文件级 `static`
上下文结构实现（参考 `mrX/test/kernel/timer.c:15` 的 `test_timer_ctx_t`），并在 每个用例内自行初始化/清理，避免用例间隐式耦合。

**Q：能只跑一个模块的测试吗？**

可以。`mr_testsuite_run("timer")` 只运行 timer suite；`mr_testcase_run` 可精确 到单个用例。是否全量运行取决于应用调用的是哪个入口；当前宿主示例工程未调用它们。

**Q：能在目标板上跑吗？**

不建议。框架依赖 `mr_printf` 输出与链接器段遍历，且用例常以宿主时钟推进 （`mr_clock_increase`）模拟时间流逝；目标板验证建议使用各模块自身的日志与
`MR_ASSERT`。

---

## 开发者指南

### 8. 架构总览

```
   MR_TEST(timer, init_del)                    （用户测试文件）
        | 展开为：静态函数 + mr_testcase_t 段变量
        v
   "mr_testcase.1" 链接器段                     （编译期注册）
        | 链接器按段名收集，sys.start/sys.end 哨兵夹住用例区
        v
   __mr_testcase_sys_start .. sys_end          （链接期形成的只读用例表）
        | 顺序遍历
        v
   testcase_run() -> cs->entry(&status)        （运行期分发）
        | status == PASS 魔数？
        v
   mr_printf 报告 + pass/fail 计数             （经 mr_port_output 出屏）
```

- 注册机制与 [初始化调用](../kernel/initcall.md) 同源：链接器段 + 哨兵符号 + 顺序遍历；哨兵 `sys/start`、`sys/end` 定义在
  `mrX/test/test.c:14-24`，段名
  `"0.end"` / `"1.end"` 保证它们排在用例区两侧。
- 运行结果只有一条信息通道：用例函数内的 `__test_status` 指针（状态机见第 10 节）。

### 9. 数据结构

```c
typedef void(mr_testcase_entry)(volatile mr_uint32_t *__test_status);

typedef struct mr_testcase {        /* mrX/include/mrX/test/mr_test.h:169 */
    const char *suite;              /* Suite name string */
    const char *name;               /* Testcase name string */
    mr_testcase_entry *entry;       /* Test function */
} mr_testcase_t;
```

关键内部约定（`mrX/include/mrX/test/mr_check.h:29`）：

- `__MR_TEST_MAGIC_PASS`（`0x70617373`，即 ASCII "pass"）：status 的"当前无失败"
  魔数；用例开始前由 `testcase_run` 预置（`mrX/test/test.c:27`）。
- 失败标记方式是 **按位取反**：`~__MR_TEST_MAGIC_PASS` 表示"已失败"。ASSERT 的 实现正是"置反魔数 → 执行检查 → 若被置反则
  return"。

### 10. 核心流程

**注册**：`MR_TEST(_suite, _name)`（`mrX/include/mrX/test/mr_test.h:183`）把
`suite`、`name` 字符串化后连同函数指针写入 `MR_USED const mr_testcase_t` 变量， 落位 `mr_testcase."1"`
段——所有用例在链接期自然聚合成一张表。

**EXPECT**（`mr_check.h:100`）：`__MR_TEST_EXPECT` 就是检查表达式本身。比较宏 （如 `__MR_TEST_COND_INT`）在条件不成立时调用
`__MR_TEST_PRINTF` 打印两个操作数 的源码文本与实际值，并把 status 置反；随后用例继续执行。

**ASSERT**（`mr_check.h:101`）：先保存当前 status，预置通过魔数，执行检查；若 status 已被置反说明检查失败，立即 `return`
（跳过用例剩余部分）；否则恢复原 status——这使 ASSERT 嵌套在 EXPECT 之后仍能正确区分"本条失败"与"此前已失败"。

**分发**：`testcase_run`（`mrX/test/test.c:26`）打印 `[ RUN ]`、调用
`cs->entry(&status)`、按最终 status 打印 `[ OK ]` / `[ FAILED ]`。三个运行入口 （`mr_testcase_run` / `mr_testsuite_run` /
`mr_test_run`，`test.c:50/80/124`） 都是对哨兵区间 `start+1 .. end` 的线性扫描，区别仅在名称匹配粒度与汇总统计；
`mr_testcase_run` / `mr_testsuite_run` 空匹配打印 `[ NOT FOUND ]` 并返回失败。

### 11. 并发与中断安全

框架面向单线程宿主环境设计， **无任何加锁**：status 是栈上变量，用例表是只读 段。三个运行入口不可并发调用；用例内部如需多线程（极少见），同步责任在用例
自身。

### 12. 扩展与派生

新增一种比较类型的路径：在 `mr_check.h` 添加 `__MR_TEST_COND_<X>` 条件宏（组合 已有的 `__MR_TEST_<COND>` 比较原语与类型/格式串），在
`mr_test.h` 按既有模式 展开为 EXPECT/ASSERT 一对宏。比较原语（EQ/NE/LT/LE/GT/GE/NEAR）与类型分派
（INT/UINT/PTR/FLOAT/DOUBLE）是两个正交维度，现有 60+ 宏均由它们组合而成， 无需新的运行时机制。

### 13. 源码导读

| 位置                                         | 职责                                    |
|----------------------------------------------|-----------------------------------------|
| `mrX/include/mrX/test/mr_test.h:183`         | `MR_TEST` 用例定义与段注册宏            |
| `mrX/include/mrX/test/mr_test.h:27-163`      | EXPECT/ASSERT 宏全集（比较 × 类型矩阵） |
| `mrX/include/mrX/test/mr_check.h:29`         | PASS 魔数与比较原语实现                 |
| `mrX/include/mrX/test/mr_check.h:101`        | ASSERT 的状态切换与提前返回             |
| `mrX/test/test.c:14-24`                      | sys.start / sys.end 哨兵用例            |
| `mrX/test/test.c:26`                         | 单用例执行与结果打印                    |
| `mrX/test/test.c:50/80/124`                  | 三个运行入口（用例/套件/全量）          |
| `mrX/test/{kernel,libc,drivers,fs,modules}/` | 各子系统的实际用例集                    |

### 14. 附录

**配置宏**

| 宏                    | 说明                                                                      |
|-----------------------|---------------------------------------------------------------------------|
| `MR_USE_TEST`         | 框架总开关（默认 n），自动选中 `MR_USE_PRINTF`                            |
| `MR_USE_TEST_PTHREAD` | 多线程并发用例门控（无 Kconfig 项），由项目根 `CMakeLists.txt:78-81` 定义 |

**错误码**：运行入口返回 `mr_bool_t`（全过 `MR_TRUE`），不使用 `mr_errno` 体系； 是否把结果映射为进程退出码由调用方（应用入口）自行决定。
