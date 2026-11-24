# 测试框架（Test）

测试框架为 mrX 提供轻量级单元测试能力：通过 `MR_TEST` 宏定义并用链接器段自动收集用例，使用
`EXPECT`/`ASSERT` 宏校验结果并输出 gtest 风格报告。

| 项目     | 说明                                                               |
|----------|--------------------------------------------------------------------|
| 功能宏   | `MR_USE_TEST`（默认 `n`，自动选中 `MR_USE_PRINTF`）                |
| 头文件   | `<mrX/test/mr_test.h>`、`<mrX/test/mr_check.h>`                    |
| 依赖     | [格式化输出](../kernel/printf.md)、链接器段收集（`mr_testcase.*`） |
| 测试用例 | `mrX/test/`（kernel / libc / drivers / fs / modules）              |

## 配置

| 配置项                | 类型     | 默认值 | 说明                                                                                                            |
|-----------------------|----------|--------|-----------------------------------------------------------------------------------------------------------------|
| `MR_USE_TEST`         | bool     | n      | 启用测试框架（自动开启 `MR_USE_PRINTF`）                                                                        |
| `MR_USE_TEST_PTHREAD` | 编译定义 | 无     | 门控基于 pthread 的并发用例（如 `test/kernel/fifo.c`、`test/fs/concurrency.c`），需由工程显式定义并链接 pthread |

测试用例不参与常规固件构建：需在应用工程中开启 `MR_USE_TEST`、纳入 `test/test.c`
及所需用例源码、保留 `mr_testcase.*` 段，并在 `mr_user_init()` 之后显式调用运行入口。

## 快速上手

```c
#include <mrX/test/mr_test.h>
#include <mrX/kernel/mr_timer.h>

#if defined(MR_USE_TEST) && defined(MR_USE_TIMER)
static void test_timer_entry(mr_timer_t *timer, void *param) {
    MR_IGNORE(timer);
    MR_IGNORE(param);
}

MR_TEST(timer, init_del) {
    mr_timer_t timer;
    mr_err_t ret;

    ret = mr_timer_init(&timer, test_timer_entry, MR_NULL, 1000);
    MR_TEST_ASSERT_EQ_INT(ret, 0); /* 前置条件失败时立即返回 */
    MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_INITED(&timer), MR_TRUE);

    ret = mr_timer_del(&timer);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
}
#endif
```

在应用入口执行全量测试并返回状态码：

```c
#include <mrX.h>
#include <mrX/test/mr_test.h>

int main(void) {
    mr_user_init();
#if defined(MR_USE_TEST)
    return mr_test_run() ? 0 : 1;
#else
    return 0;
#endif
}
```

## 核心机制与约束

- **`EXPECT` 与 `ASSERT`**：`MR_TEST_EXPECT_*` 失败时记录错误并继续执行后续检查；
  `MR_TEST_ASSERT_*` 失败时立即 `return` 退出当前用例。需要保证资源释放的用例建议前置用
  `ASSERT`，后续检查用 `EXPECT` 配合 `goto _exit` 统一清理。
- **类型后缀与容差**：断言后缀必须与操作数类型匹配（`INT` 有符号、`UINT` 无符号、
  `PTR` 指针、`FLOAT`/`DOUBLE` 浮点）；`*_EQ_FLOAT` 与 `*_EQ_DOUBLE` 内置容差分别为
  `1e-4` 和 `1e-8`，自定义容差使用 `MR_TEST_*_NEAR(a, b, eps)`。
- **返回值边界**：`mr_test_run()` 在没有任何用例被链接时仍可能返回 `MR_TRUE`，接入
  CI 时应结合输出统计确认实际执行用例数；`mr_testsuite_run()` 与
  `mr_testcase_run()` 在未匹配到用例时打印 `[ NOT FOUND ]` 并返回 `MR_FALSE`。
- **单线程约束**：测试框架内部无锁，三个运行入口不可并发调用；跨用例共享上下文需在文件内用
  `static` 变量自行初始化与清理。

## API 速查

### 用例定义与运行

| API                            | 说明                                                     |
|--------------------------------|----------------------------------------------------------|
| `MR_TEST(suite, name)`         | 定义并向 `mr_testcase.1` 段注册测试用例                  |
| `mr_test_run()`                | 运行所有已注册用例，全部通过返回 `MR_TRUE`               |
| `mr_testsuite_run(suite)`      | 运行指定套件的全部用例，至少执行一个且全过返回 `MR_TRUE` |
| `mr_testcase_run(suite, name)` | 运行单个指定用例，存在且通过返回 `MR_TRUE`               |

### 断言宏矩阵

所有断言宏均以 `MR_TEST_EXPECT_`（失败继续）或 `MR_TEST_ASSERT_`（失败返回）为前缀，未启用
`MR_USE_TEST` 时自动展开为空：

| 比较             | `int`     | `uint`     | `ptr`     | `float` / `double`         | 自定义容差         |
|------------------|-----------|------------|-----------|----------------------------|--------------------|
| 相等（`==`）     | `_EQ_INT` | `_EQ_UINT` | `_EQ_PTR` | `_EQ_FLOAT` / `_EQ_DOUBLE` | `_NEAR(a, b, eps)` |
| 不等（`!=`）     | `_NE_INT` | `_NE_UINT` | `_NE_PTR` | `_NE_FLOAT` / `_NE_DOUBLE` | —                  |
| 小于（`<`）      | `_LT_INT` | `_LT_UINT` | `_LT_PTR` | `_LT_FLOAT` / `_LT_DOUBLE` | —                  |
| 小于等于（`<=`） | `_LE_INT` | `_LE_UINT` | `_LE_PTR` | `_LE_FLOAT` / `_LE_DOUBLE` | —                  |
| 大于（`>`）      | `_GT_INT` | `_GT_UINT` | `_GT_PTR` | `_GT_FLOAT` / `_GT_DOUBLE` | —                  |
| 大于等于（`>=`） | `_GE_INT` | `_GE_UINT` | `_GE_PTR` | `_GE_FLOAT` / `_GE_DOUBLE` | —                  |

## 内部实现

```text
MR_TEST(suite, name) ──► mr_testcase_t 变量写入 "mr_testcase.1" 段
                              │ 链接期由 sys.start ("0.end") 与 sys.end ("1.end") 夹住
                              ▼
testcase_run() ──► 预置 status = __MR_TEST_MAGIC_PASS (0x70617373) ──► cs->entry(&status)
```

- **段收集机制**：`test/test.c` 通过 `__MR_TEST_EXPORT` 在 `"0.end"` 与 `"1.end"`
  放置哨兵符号 `__mr_testcase_sys_start` 和 `__mr_testcase_sys_end`，运行入口线性扫描其间的
  `mr_testcase_t` 数组。
- **状态魔数切换**：`include/mrX/test/mr_check.h` 使用栈上变量 `__test_status`
  传递用例结果——断言失败时将其按位取反（`~__MR_TEST_MAGIC_PASS`）。
  `__MR_TEST_ASSERT` 执行检查前会先保存原状态、临时置为 `PASS`，仅当本次检查导致状态被取反时才
  `return`，从而保证 `EXPECT` 失败后紧跟的 `ASSERT` 仍能准确判断自身结果。
