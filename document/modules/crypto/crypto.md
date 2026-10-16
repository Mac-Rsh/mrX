# 密码算法（Crypto）

> Crypto 模块提供不依赖平台硬件的 SHA-256、HMAC-SHA-256、NIST P-256 与 ECDSA P-256 验签。

| 项目     | 说明                                                                                                                                                                  |
|----------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_MODULE_CRYPTO`；子功能见第 2 节                                                                                                                               |
| 头文件   | `<mrX/modules/crypto/sha/mr_sha256.h>`、`<mrX/modules/crypto/sha/mr_hmac_sha256.h>`、`<mrX/modules/crypto/ecc/mr_p256.h>`、`<mrX/modules/crypto/ecc/mr_ecdsa_p256.h>` |
| 依赖模块 | `MR_USE_MODULE`；HMAC 自动选中 SHA-256；ECDSA 自动选中 P-256                                                                                                          |
| 实现位置 | `mrX/modules/crypto/sha/sha256.c`、`mrX/modules/crypto/sha/hmac_sha256.c`、`mrX/modules/crypto/ecc/p256.c`、`mrX/modules/crypto/ecc/ecdsa_p256.c`                     |

---

## 用户指南

### 1. 模块概述

Crypto 当前提供四组能力：

- SHA-256：支持分段输入和一次性计算，输出 32 字节摘要。
- HMAC-SHA-256：使用调用方提供的密钥计算 32 字节认证标签，支持分段和一次性计算。
- P-256：提供公开的曲线值、点、工作对象以及 ECDSA 所需的基础标量和点运算。
- ECDSA P-256：验证以 NIST P-256 为基础、针对 32 字节摘要的固定宽度 `r/s` 签名。

ECDSA P-256 建立在 P-256 基础层之上。P-256 当前不提供私钥生成、公钥派生等上层密钥管理接口。本模块不解析 PEM、DER、X.509
或公钥证书，也不提供随机数、密钥存储、密钥交换和签名生成。

### 2. 配置与依赖

| 配置项                             | 默认 | 说明                             |
|------------------------------------|------|----------------------------------|
| `MR_USE_MODULE`                    | y    | 上层模块总开关                   |
| `MR_USE_MODULE_CRYPTO`             | n    | Crypto 总开关                    |
| `MR_USE_MODULE_CRYPTO_SHA256`      | n    | SHA-256                          |
| `MR_USE_MODULE_CRYPTO_HMAC_SHA256` | n    | HMAC-SHA-256；自动选中 SHA-256   |
| `MR_USE_MODULE_CRYPTO_P256`        | n    | NIST P-256 基础运算              |
| `MR_USE_MODULE_CRYPTO_ECDSA_P256`  | n    | ECDSA P-256 验签；自动选中 P-256 |

模块头文件不由 `<mrX.h>` 代为引入，使用时应显式包含需要的 Crypto 头文件。

### 3. SHA-256 快速上手

一次性计算：

```c
#include <mrX/modules/crypto/sha/mr_sha256.h>

static mr_uint8_t digest[MR_SHA256_DIGEST_SIZE];
static const char message[] = "mrX";

mr_sha256(message, sizeof(message) - 1U, digest);
```

分段计算：

```c
mr_sha256_t ctx;
mr_uint8_t digest[MR_SHA256_DIGEST_SIZE];

mr_sha256_init(&ctx);
mr_sha256_update(&ctx, header, header_size);
mr_sha256_update(&ctx, payload, payload_size);
mr_sha256_final(&ctx, digest);
```

`mr_sha256_final` 会在输出摘要后清零上下文。若要重新计算，必须再次调用 `mr_sha256_init`。

#### HMAC-SHA-256

启用 `MR_USE_MODULE_CRYPTO_HMAC_SHA256` 后，可一次性计算或分段输入；该配置自动选中 SHA-256。

```c
#include <mrX/modules/crypto/sha/mr_hmac_sha256.h>

mr_uint8_t auth[MR_SHA256_DIGEST_SIZE];
mr_hmac_sha256(key, key_size, message, message_size, auth);
```

分段计算依次调用 `mr_hmac_sha256_init`、`mr_hmac_sha256_update` 和 `mr_hmac_sha256_final`。
`final` 写出标签后清零工作对象；空密钥或空输入可使用 `MR_NULL`，对应长度必须为 0。

### 4. P-256 基础层

P-256 公共头文件为 `<mrX/modules/crypto/ecc/mr_p256.h>`。其中
`mr_p256_t` 承载曲线运算所需的大块临时数据，`mr_p256_value_t` 表示 256 位值，`mr_p256_point_t` 表示仿射点，
`mr_p256_public_key_t` 保存 32 字节大端 `x` 和 `y` 坐标。

当前公开 API 提供值加载、标量模运算、点合法性检查、标量乘和点加， 供 ECDSA-P256 等上层算法复用。当前标量乘实现不是常数时间实现，不应在
未经侧信道评估时直接用于秘密私钥标量运算。

### 5. ECDSA P-256 验签

`mr_ecdsa_p256_verify` 接收已经计算好的 32 字节摘要，不是原始消息。下例使用 SHA-256 生成摘要；ECDSA-P256 配置本身只依赖
P-256。签名是 `r` 和 `s` 各 32 字节大端整数的原始固定宽度形式，不是 ASN.1 DER 序列。公钥为 P-256 仿射坐标，`x` 和 `y` 各
32 字节大端数据；结构体不包含 SEC1 未压缩格式的 `0x04` 前缀。

```c
#include <mrX/modules/crypto/sha/mr_sha256.h>
#include <mrX/modules/crypto/ecc/mr_ecdsa_p256.h>

mr_uint8_t digest[MR_SHA256_DIGEST_SIZE];
static mr_ecdsa_p256_t ecdsa;
mr_p256_public_key_t public_key = { /* x 与 y */ };
mr_ecdsa_p256_signature_t signature = { /* r 与 s */ };

mr_sha256(message, message_size, digest);
if (!mr_ecdsa_p256_verify(&ecdsa, &public_key, digest, &signature)) {
    /* 公钥非法、签名格式错误或验签不匹配。 */
}
```

返回 `MR_TRUE` 表示签名有效；公钥非法、`r/s` 超出范围或验签不匹配都返回 `MR_FALSE`。
`mr_ecdsa_p256_t` 承载验签过程中的大块临时数据，可定义为静态对象以避免占用线程栈。

### 6. API 参考

| API                           | 说明                              |
|-------------------------------|-----------------------------------|
| `mr_sha256_init`              | 初始化可分段更新的 SHA-256 上下文 |
| `mr_sha256_update`            | 追加任意长度输入                  |
| `mr_sha256_final`             | 输出 32 字节摘要并清零上下文      |
| `mr_sha256`                   | 一次性计算完整缓冲区              |
| `mr_hmac_sha256_init`         | 以给定密钥初始化 HMAC 上下文      |
| `mr_hmac_sha256_update`       | 追加认证数据                      |
| `mr_hmac_sha256_final`        | 输出 32 字节标签并清零上下文      |
| `mr_hmac_sha256`              | 一次性计算 HMAC-SHA-256           |
| `mr_p256_value_from_be`       | 加载 32 字节大端 P-256 值         |
| `mr_p256_scalar_valid`        | 校验标量范围                      |
| `mr_p256_scalar_reduce`       | 按曲线阶约减标量                  |
| `mr_p256_scalar_inv`          | 计算标量模逆                      |
| `mr_p256_scalar_mul`          | 计算标量模乘                      |
| `mr_p256_point_from_public`   | 将公钥坐标载入仿射点              |
| `mr_p256_point_valid`         | 校验 P-256 点                     |
| `mr_p256_point_mul_generator` | 计算生成元标量乘                  |
| `mr_p256_point_mul`           | 计算点标量乘                      |
| `mr_p256_point_add`           | 计算点加                          |
| `mr_ecdsa_p256_verify`        | 验证 32 字节摘要的原始 `r/s` 签名 |

工作对象和输出缓冲区不能为 `MR_NULL`。SHA-256/HMAC 的输入数据为空时可传 `MR_NULL`，
HMAC 的密钥为空时也可传 `MR_NULL`；这两种情况对应的长度必须为 0。其他非法指针在断言开启时由 `MR_ASSERT` 拦截。

### 7. 常见问题（FAQ）

**DER 签名能否直接传入？**

不能。应先由上层解析 DER，再将 `r` 和 `s` 各自左侧补零到 32 字节。

**公钥为什么只有 64 字节？**

结构体只保存 `x || y`。若外部数据采用 65 字节 SEC1 未压缩格式，先校验并去掉首字节 `0x04`。

**能否把这些接口当成完整安全协议？**

不能。协议还需明确的消息编码、密钥信任、重放防护、密钥存储和错误处理策略。

---

## 开发者指南

### 8. 架构与数据流

SHA-256 上下文保存 8 个状态字、64 字节分组缓冲区、当前缓冲长度和总输入字节数。`update` 填满分组后调用压缩函数，`final` 写入
SHA-256 padding 和 64 位 bit length。HMAC-SHA-256 复用 SHA-256，对密钥按 64 字节分组处理，并保存 inner/outer 两个上下文。

P-256 以 8 个 32 位字表示 256 位值。点倍加和标量乘在 Jacobian 坐标中运行，输出时转回仿射坐标。P-256 提供基础曲线
API，ECDSA-P256 通过这些公开 API 完成验签，不直接依赖 P-256 文件内部 helper。

### 9. 并发与上下文

- 模块没有可变全局状态，不同调用者使用不同 `mr_sha256_t` 时可以并发计算。
- 同一 `mr_sha256_t` 或 `mr_hmac_sha256_t` 不应被多个上下文并发修改。
- P-256 的大块临时数据保存在调用者提供的 `mr_p256_t` 中；同一对象不能并发用于多个曲线运算。
- ECDSA-P256 验签的大块临时数据保存在调用者提供的 `mr_ecdsa_p256_t` 中；同一对象不能并发用于多个验签。
- 若希望降低线程栈占用，应将 `mr_ecdsa_p256_t` 定义为静态或其他非栈对象。
- 实现没有对外承诺常数时间执行，不应在未经专门侧信道评估时假定它具有相关防护。

### 10. 扩展与源码导读

| 位置                                                  | 职责                               |
|-------------------------------------------------------|------------------------------------|
| `mrX/include/mrX/modules/crypto/Kconfig`              | Crypto 及四个子功能开关            |
| `mrX/include/mrX/modules/crypto/sha/mr_sha256.h`      | SHA-256 常量、上下文和公开 API     |
| `mrX/modules/crypto/sha/sha256.c`                     | 分组压缩、分段更新与收尾           |
| `mrX/include/mrX/modules/crypto/sha/mr_hmac_sha256.h` | HMAC 上下文与公开 API              |
| `mrX/modules/crypto/sha/hmac_sha256.c`                | HMAC 密钥处理、分段计算与清理      |
| `mrX/include/mrX/modules/crypto/ecc/mr_p256.h`        | P-256 公共类型、工作对象与基础 API |
| `mrX/modules/crypto/ecc/p256.c`                       | P-256 大整数、标量运算与曲线点运算 |
| `mrX/include/mrX/modules/crypto/ecc/mr_ecdsa_p256.h`  | ECDSA 对象、签名类型与验签 API     |
| `mrX/modules/crypto/ecc/ecdsa_p256.c`                 | 基于 P-256 公共 API 的 ECDSA 验签  |

增加算法时，应为它增加独立 Kconfig 开关和头文件，避免用户仅为一个算法引入整个 Crypto 实现。
