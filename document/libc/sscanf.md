# sscanf - 格式化输入

## 1. 模块概述

### 1.1 功能介绍

sscanf 模块提供格式化字符串输入功能，从字符串中读取格式化数据。它是 sprintf 的反向操作，类似于标准 C 库的 `sscanf` 函数。

该模块支持两种配置模式：STD 模式（使用标准库）和自定义模式（使用内置实现）。

### 1.2 主要特性

- **格式化输入**：支持 scanf 风格的格式说明符
- **字符串解析**：从字符串中提取数据
- **标准兼容**：接口与标准 C 库一致

### 1.3 适用场景

sscanf 模块适用于以下场景：

- **命令解析**：解析命令行字符串
- **配置解析**：解析配置文件内容
- **数据转换**：将字符串转换为数值
- **协议解析**：解析网络协议数据

---

## 2. 使用指南

### 2.1 接口说明

#### mr_sscanf() - 格式化输入

```c
int mr_sscanf(const char *buf, const char *fmt, ...);
```

**功能**：从 buf 中根据格式字符串 fmt 读取数据

**参数**：

- `buf`：源字符串
- `fmt`：格式字符串
- `...`：指向变量的指针（接收数据）

**返回值**：

- 成功：返回成功转换的项目数
- 失败：返回负值或 0（没有项目被转换）

**示例**：

```c
#include <mrX/libc/mr_sscanf.h>

char buffer[] = "42 3.14 hello";
int a;
float b;
char c[16];

int ret = mr_sscanf(buffer, "%d %f %s", &a, &b, c);
printf("Converted: %d items\n", ret);
printf("a=%d, b=%.2f, c=%s\n", a, b, c);
/* 输出:
   Converted: 3 items
   a=42, b=3.14, c=hello
*/
```

### 2.2 格式说明符

sscanf 支持常用的 scanf 格式说明符：

#### 整数格式

| 说明符 | 类型 | 示例 |
|--------|------|------|
| `%d` | 有符号十进制 int* | `int val; sscanf("42", "%d", &val);` |
| `%u` | 无符号十进制 int* | `unsigned val; sscanf("42", "%u", &val);` |
| `%x` | 无符号十六进制 int* | `int val; sscanf("ff", "%x", &val);` |
| `%o` | 无符号八进制 int* | `int val; sscanf("10", "%o", &val);` |

#### 字符格式

| 说明符 | 类型 | 示例 |
|--------|------|------|
| `%c` | 字符 char* | `char c; sscanf("A", "%c", &c);` |
| `%s` | 字符串 char* | `char str[16]; sscanf("hello", "%s", str);` |

**注意**：`%s` 遇到空白字符（空格、制表符、换行）停止。

#### 宽度修饰符

```c
char buffer[16];

/* 限制读取的字符数 */
mr_sscanf("1234567890", "%5s", buffer);  /* buffer = "12345" */

/* 读取固定数量的字符 */
char c1, c2, c3;
mr_sscanf("ABC", "%c%c%c", &c1, &c2, &c3);  /* c1='A', c2='B', c3='C' */
```

#### 跳过空白

```c
int a, b, c;

/* 默认跳过空白 */
mr_sscanf("  42   100  ", "%d %d", &a, &b);  /* a=42, b=100 */

/* 使用 %c 读取包括空白 */
char space, ch;
mr_sscanf(" A", "%c%c", &space, &ch);  /* space=' ', ch='A' */
```

#### 赋值抑制（不存储）

```c
int a, c;

/* %*s 跳过字符串 */
mr_sscanf("42 hello 100", "%d %*s %d", &a, &c);  /* a=42, c=100 */
```

#### 长度修饰符

```c
/* 短整型 */
short val;
mr_sscanf("42", "%hd", &val);

/* 长整型 */
long val;
mr_sscanf("42", "%ld", &val);

/* 长长整型 */
long long val;
mr_sscanf("42", "%lld", &val);
```

### 2.3 配置模式

#### STD 模式（标准库模式）

```c
#define MR_USE_SSCANF_STD
```

**特点**：

- 直接使用标准库的 `<stdio.h>`
- 函数映射到标准 sscanf

```c
#if defined(MR_USE_SSCANF_STD)
#define mr_sscanf(_b, _fmt, ...)        sscanf(_b, _fmt, ##__VA_ARGS__)
#endif
```

#### 自定义模式

**特点**：

- 使用内置实现
- 支持常用格式说明符
- 适合裸机环境

### 2.4 典型应用案例

#### 案例 1：命令解析

```c
#include <mrX/libc/mr_sscanf.h>

void parse_command(const char *cmd) {
    char command[16];
    int arg1, arg2;
    int ret;

    /* 尝试解析带两个参数的命令 */
    ret = mr_sscanf(cmd, "%15s %d %d", command, &arg1, &arg2);

    if (ret >= 1) {
        printf("Command: %s\n", command);
        if (ret >= 2) {
            printf("Arg1: %d\n", arg1);
        }
        if (ret >= 3) {
            printf("Arg2: %d\n", arg2);
        }
    }
}

/* 使用 */
parse_command("set 42 100");  /* Command: set, Arg1: 42, Arg2: 100 */
parse_command("get 42");      /* Command: get, Arg1: 42 */
parse_command("help");        /* Command: help */
```

#### 案例 2：MAC 地址解析

```c
#include <mrX/libc/mr_sscanf.h>

mr_err_t parse_mac(const char *str, mr_uint8_t *mac) {
    int a, b, c, d, e, f;
    int ret;

    /* 解析 XX:XX:XX:XX:XX:XX 格式 */
    ret = mr_sscanf(str, "%x:%x:%x:%x:%x:%x", &a, &b, &c, &d, &e, &f);

    if (ret != 6) {
        return -MR_EINVAL;  /* 解析失败 */
    }

    mac[0] = (mr_uint8_t)a;
    mac[1] = (mr_uint8_t)b;
    mac[2] = (mr_uint8_t)c;
    mac[3] = (mr_uint8_t)d;
    mac[4] = (mr_uint8_t)e;
    mac[5] = (mr_uint8_t)f;

    return 0;
}

/* 使用 */
const char *mac_str = "00:11:22:33:44:55";
mr_uint8_t mac[6];

if (parse_mac(mac_str, mac) == 0) {
    printf("MAC parsed successfully\n");
}
```

#### 案例 3：IP 地址解析

```c
#include <mrX/libc/mr_sscanf.h>

mr_err_t parse_ipv4(const char *str, mr_uint8_t *ip) {
    int a, b, c, d;
    int ret;

    /* 解析 XXX.XXX.XXX.XXX 格式 */
    ret = mr_sscanf(str, "%d.%d.%d.%d", &a, &b, &c, &d);

    if (ret != 4) {
        return -MR_EINVAL;
    }

    /* 验证范围 */
    if (a < 0 || a > 255 || b < 0 || b > 255 ||
        c < 0 || c > 255 || d < 0 || d > 255) {
        return -MR_EINVAL;
    }

    ip[0] = (mr_uint8_t)a;
    ip[1] = (mr_uint8_t)b;
    ip[2] = (mr_uint8_t)c;
    ip[3] = (mr_uint8_t)d;

    return 0;
}

/* 使用 */
const char *ip_str = "192.168.1.1";
mr_uint8_t ip[4];

if (parse_ipv4(ip_str, ip) == 0) {
    printf("IP parsed: %u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);
}
```

#### 案例 4：键值对解析

```c
#include <mrX/libc/mr_sscanf.h>
#include <mrX/libc/mr_string.h>

typedef struct {
    char key[32];
    char value[64];
} kv_pair_t;

mr_err_t parse_kv_pair(const char *line, kv_pair_t *kv) {
    const char *equals = mr_strchr(line, '=');
    int ret;

    if (equals == MR_NULL) {
        return -MR_EINVAL;
    }

    /* 解析 key=value 格式 */
    ret = mr_sscanf(line, "%31[^=]=%63s", kv->key, kv->value);

    if (ret != 2) {
        return -MR_EINVAL;
    }

    /* 去除 key 的尾部空格 */
    mr_size_t len = mr_strlen(kv->key);
    while (len > 0 && kv->key[len - 1] == ' ') {
        kv->key[--len] = '\0';
    }

    return 0;
}

/* 使用 */
kv_pair_t kv;

parse_kv_pair("timeout = 30", &kv);
printf("Key: '%s', Value: '%s'\n", kv.key, kv.value);
/* 输出: Key: 'timeout', Value: '30' */
```

#### 案例 5：配置行解析

```c
#include <mrX/libc/mr_sscanf.h>

void parse_config_line(const char *line) {
    char key[32];
    int value;
    char str_value[64];
    int ret;

    /* 尝试解析 key=value（数值） */
    ret = mr_sscanf(line, "%31[^=]=%d", key, &value);

    if (ret == 2) {
        printf("Config: %s = %d (integer)\n", key, value);
        return;
    }

    /* 尝试解析 key=value（字符串） */
    ret = mr_sscanf(line, "%31[^=]=%63s", key, str_value);

    if (ret == 2) {
        /* 去除引号 */
        if (str_value[0] == '"') {
            mr_size_t len = mr_strlen(str_value);
            if (str_value[len - 1] == '"') {
                str_value[len - 1] = '\0';
                mr_strcpy(str_value, str_value + 1);
            }
        }
        printf("Config: %s = %s (string)\n", key, str_value);
        return;
    }

    printf("Invalid config line: %s\n", line);
}

/* 使用 */
parse_config_line("baudrate = 115200");     /* Config: baudrate = 115200 (integer) */
parse_config_line("device = \"/dev/tty\""); /* Config: device = "/dev/tty" (string) */
```

#### 案例 6：时间解析

```c
#include <mrX/libc/mr_sscanf.h>

typedef struct {
    int hour;
    int minute;
    int second;
} time_t;

mr_err_t parse_time(const char *str, time_t *time) {
    int ret;

    /* 解析 HH:MM:SS 格式 */
    ret = mr_sscanf(str, "%d:%d:%d", &time->hour, &time->minute, &time->second);

    if (ret != 3) {
        return -MR_EINVAL;
    }

    /* 验证范围 */
    if (time->hour < 0 || time->hour > 23 ||
        time->minute < 0 || time->minute > 59 ||
        time->second < 0 || time->second > 59) {
        return -MR_EINVAL;
    }

    return 0;
}

/* 使用 */
const char *time_str = "14:30:45";
time_t time;

if (parse_time(time_str, &time) == 0) {
    printf("Time: %02d:%02d:%02d\n", time.hour, time.minute, time.second);
}
```

---

## 3. 设计思路

### 3.1 设计目标

sscanf 模块的设计目标：

1. **标准兼容**：与 scanf 接口一致
2. **简单易用**：支持常用格式说明符
3. **可移植性**：不依赖复杂的标准库
4. **轻量级**：最小化实现

### 3.2 解析处理流程

```
扫描格式字符串
    ↓
遇到 '%' 开始格式说明符
    ↓
读取修饰符和类型
    ↓
从输入字符串匹配
    ↓
转换并存储到变量
    ↓
返回成功转换的项目数
```

### 3.3 设计权衡

#### sscanf vs 其他解析方法

| 方法 | 优点 | 缺点 | 适用场景 |
|------|------|------|---------|
| sscanf | 简单、灵活 | 不擅长复杂格式 | 简单格式解析 |
| strtok + atoi | 精确控制 | 需要多步处理 | 分隔符分割 |
| 自定义解析器 | 完全控制 | 开发成本高 | 复杂协议 |

#### 返回值的意义

```c
int ret = mr_sscanf(str, "%d %d %s", &a, &b, c);

/* ret 的含义：
 *  0  - 没有项目被转换
 *  1  - 只有 a 被转换
 *  2  - a 和 b 被转换
 *  3  - a、b 和 c 都被转换
 * -1  - 解析错误
 */
```

---

## 4. 参考资料

### 4.1 相关模块

sscanf 模块与以下模块相关：

- **sprintf**：格式化输出（反向操作）
- **string**：字符串操作函数
- **atoi/atol**：字符串转数值函数

### 4.2 相关标准

- **ISO C**：stdio.h 中 scanf 系列函数的规范
- **POSIX**：扩展的格式说明符

### 4.3 延伸阅读

- **Scanf 格式说明符**：完整的格式说明符参考
- **字符串解析技术**：各种字符串解析方法
- **输入验证**：如何安全地验证解析结果
