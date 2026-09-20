# 密码算法（Crypto）

> Crypto 模块提供不依赖平台硬件的 SHA-256、NIST P-256 密钥运算与 ECDSA P-256 验签。

| 项目     | 说明 |
|----------|------|
| 功能宏   | `MR_USE_MODULE_CRYPTO`；子功能见第 2 节 |
| 头文件   | `<mrX/modules/crypto/sha/mr_sha256.h>`、`<mrX/modules/crypto/ecc/mr_p256.h>`、`<mrX/modules/crypto/ecc/mr_ecdsa_p256.h>` |
| 依赖模块 | `MR_USE_MODULE`；ECDSA 自动选中 SHA-256 和 P-256 |
| 实现位置 | `mrX/modules/crypto/sha/sha256.c`、`mrX/modules/crypto/ecc/p256.c` |

---

## 用户指南

### 1. 模块概述

Crypto 当前提供三组能力：

- SHA-256：支持分段输入和一次性计算，输出 32 字节摘要。
- P-256：校验 32 字节私钥、64 字节仿射公钥，以及从私钥派生公钥。
- ECDSA P-256：验证以 P-256 和 SHA-256 为基础的固定宽度 `r/s` 签名。

本模块是底层算法层，不解析 PEM、DER、X.509 或公钥证书，也不提供随机数、密钥存储、密钥交换和签名生成。

### 2. 配置与依赖

| 配置项                              | 默认 | 说明 |
|-------------------------------------|------|------|
| `MR_USE_MODULE`                     | y    | 上层模块总开关 |
| `MR_USE_MODULE_CRYPTO`              | n    | Crypto 总开关 |
| `MR_USE_MODULE_CRYPTO_SHA256`       | n    | SHA-256 |
| `MR_USE_MODULE_CRYPTO_ECC_P256`     | n    | P-256 密钥校验与公钥派生 |
| `MR_USE_MODULE_CRYPTO_ECDSA_P256`   | n    | ECDSA P-256 验签；自动选中前两项 |

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

### 4. P-256 密钥

P-256 私钥是 32 字节大端整数，有效范围为 `[1, n-1]`。公钥为未压缩仿射坐标，`x` 和 `y` 各 32 字节、均为大端；`mr_p256_public_key_t` 不包含 SEC1 未压缩格式的 `0x04` 前缀。

```c
#include <mrX/modules/crypto/ecc/mr_p256.h>

mr_uint8_t private_key[MR_P256_SCALAR_SIZE] = { /* secure input */ };
mr_p256_public_key_t public_key;

if (!mr_p256_private_key_valid(private_key)) {
    return -MR_EINVAL;
}
if (mr_p256_public_key(private_key, &public_key) != 0) {
    return -MR_EIO;
}
```

`mr_p256_public_key_valid` 检查坐标范围及曲线方程，可在使用外部公钥前调用。

### 5. ECDSA P-256 验签

`mr_ecdsa_p256_verify` 接收的是已经计算好的 32 字节 SHA-256 摘要，不是原始消息。签名是 `r` 和 `s` 各 32 字节大端整数的原始固定宽度形式，不是 ASN.1 DER 序列。

```c
#include <mrX/modules/crypto/ecc/mr_ecdsa_p256.h>

mr_uint8_t digest[MR_SHA256_DIGEST_SIZE];
mr_p256_public_key_t public_key = { /* x and y */ };
mr_ecdsa_p256_signature_t signature = { /* r and s */ };

mr_sha256(message, message_size, digest);
if (!mr_ecdsa_p256_verify(&public_key, digest, &signature)) {
    /* Invalid public key, malformed signature, or signature mismatch. */
}
```

返回 `MR_TRUE` 表示签名有效；公钥非法、`r/s` 超出范围或验签不匹配都返回 `MR_FALSE`。

### 6. API 参考

| API | 说明 |
|-----|------|
| `mr_sha256_init` | 初始化可分段更新的 SHA-256 上下文 |
| `mr_sha256_update` | 追加任意长度输入 |
| `mr_sha256_final` | 输出 32 字节摘要并清零上下文 |
| `mr_sha256` | 一次性计算完整缓冲区 |
| `mr_p256_private_key_valid` | 校验 32 字节大端私钥 |
| `mr_p256_public_key_valid` | 校验仿射公钥点 |
| `mr_p256_public_key` | 从私钥派生仿射公钥 |
| `mr_ecdsa_p256_verify` | 验证 SHA-256 digest 的原始 `r/s` 签名 |

所有指针参数都是前置条件，空指针在断言开启时由 `MR_ASSERT` 拦截。`mr_p256_public_key` 在私钥无效时返回 `-MR_EINVAL`，无法生成有限公钥点时返回 `-MR_EIO`。

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

SHA-256 上下文保存 8 个状态字、64 字节分组缓冲区、当前缓冲长度和总输入字节数。`update` 填满分组后调用压缩函数，`final` 写入 SHA-256 padding 和 64 位 bit length。

P-256 内部以 8 个 32 位字表示大整数，对外统一转换为 32 字节大端序列。点倍加和标量乘在 Jacobian 坐标中运行，输出时转回仿射坐标。ECDSA 验签复用同一标量和点运算实现。

### 9. 并发与上下文

- 模块没有可变全局状态，不同调用者使用不同 `mr_sha256_t` 时可以并发计算。
- 同一 `mr_sha256_t` 不应被多个上下文并发修改。
- P-256/ECDSA 函数使用调用栈上的临时数据，不调用分配器；可用栈空间需由目标环境评估。
- 实现没有对外承诺常数时间执行，不应在未经专门侧信道评估时假定它具有相关防护。

### 10. 扩展与源码导读

| 位置 | 职责 |
|------|------|
| `mrX/include/mrX/modules/crypto/Kconfig` | Crypto 及三个子功能开关 |
| `mrX/include/mrX/modules/crypto/sha/mr_sha256.h` | SHA-256 常量、上下文和公开 API |
| `mrX/modules/crypto/sha/sha256.c` | 分组压缩、分段更新与收尾 |
| `mrX/include/mrX/modules/crypto/ecc/mr_p256.h` | P-256 公开密钥类型与 API |
| `mrX/include/mrX/modules/crypto/ecc/mr_ecdsa_p256.h` | ECDSA 签名类型与验签 API |
| `mrX/modules/crypto/ecc/p256.c` | 大整数、曲线点运算、公钥派生与验签 |

增加算法时，应为它增加独立 Kconfig 开关和头文件，避免用户仅为一个算法引入整个 Crypto 实现。
