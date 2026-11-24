# 密码算法（Crypto）

不依赖平台硬件的纯软件密码学基础库，提供 SHA-256、HMAC-SHA-256、NIST P-256 曲线运算与
ECDSA P-256 验签。

| 项目   | 说明                                                                                                                                                                  |
|--------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 功能宏 | `MR_USE_MODULE_CRYPTO`（各子算法独立开关见配置表）                                                                                                                    |
| 头文件 | `<mrX/modules/crypto/sha/mr_sha256.h>`、`<mrX/modules/crypto/sha/mr_hmac_sha256.h>`、`<mrX/modules/crypto/ecc/mr_p256.h>`、`<mrX/modules/crypto/ecc/mr_ecdsa_p256.h>` |
| 依赖   | 无外部模块依赖（需开启 `MR_USE_MODULE`）                                                                                                                              |
| 测试   | `test/modules/crypto/crypto.c`                                                                                                                                        |

## 配置

| 配置项                             | 类型 | 默认值 | 说明                                               |
|------------------------------------|------|--------|----------------------------------------------------|
| `MR_USE_MODULE_CRYPTO`             | bool | n      | Crypto 总开关                                      |
| `MR_USE_MODULE_CRYPTO_SHA256`      | bool | n      | 启用 SHA-256 摘要计算                              |
| `MR_USE_MODULE_CRYPTO_HMAC_SHA256` | bool | n      | 启用 HMAC-SHA-256 认证标签计算（自动选中 SHA-256） |
| `MR_USE_MODULE_CRYPTO_P256`        | bool | n      | 启用 NIST P-256（secp256r1）曲线基础运算           |
| `MR_USE_MODULE_CRYPTO_ECDSA_P256`  | bool | n      | 启用 ECDSA P-256 验签（自动选中 P-256）            |

## 快速上手

Crypto 头文件不由 `<mrX.h>` 自动引入，按需显式包含对应子模块头文件即可：

```c
#include <mrX/modules/crypto/sha/mr_sha256.h>
#include <mrX/modules/crypto/sha/mr_hmac_sha256.h>
#include <mrX/modules/crypto/ecc/mr_ecdsa_p256.h>

/* 1. SHA-256 一次性计算与分段计算 */
mr_uint8_t digest[MR_SHA256_DIGEST_SIZE];
mr_sha256(msg, msg_size, digest);

mr_sha256_t sha_ctx;
mr_sha256_init(&sha_ctx);
mr_sha256_update(&sha_ctx, header, header_size);
mr_sha256_update(&sha_ctx, payload, payload_size);
mr_sha256_final(&sha_ctx, digest);

/* 2. HMAC-SHA-256 计算 */
mr_uint8_t auth[MR_SHA256_DIGEST_SIZE];
mr_hmac_sha256(key, key_size, msg, msg_size, auth);

/* 3. ECDSA P-256 验签（输入为 32 字节摘要，而非原始消息） */
static mr_ecdsa_p256_t ecdsa;                  /* 工作对象较大，建议静态分配避免栈溢出 */
mr_p256_public_key_t pub_key = { /* 32B x, 32B y */ };
mr_ecdsa_p256_signature_t sig = { /* 32B r, 32B s */ };

if (!mr_ecdsa_p256_verify(&ecdsa, &pub_key, digest, &sig)) {
    /* 公钥非法、r/s 越界或验签不匹配 */
}
```

## 核心机制与约束

- **密钥与签名原始格式**：`mr_p256_public_key_t` 仅包含 `x`、`y` 各 32 字节大端坐标，不包含
  SEC1 未压缩格式首字节 `0x04`；`mr_ecdsa_p256_signature_t` 为 `r`、`s` 各 32
  字节定长大端整数，不支持直接传入 ASN.1 DER 编码或 PEM/X.509 证书（需由上层先行解析并左侧补零）。
- **工作区与上下文清零**：`mr_sha256_final()`、`mr_hmac_sha256_final()` 与
  `mr_ecdsa_p256_verify()` 返回前都会自动清零工作对象；重复使用须重新 `init`。
  `mr_p256_t` 与 `mr_ecdsa_p256_t` 体积较大，线程栈受限时应静态或堆上分配。
- **侧信道说明**：P-256 标量乘当前 **不是常数时间实现**
  ，不应在未经专门侧信道评估时直接用于秘密私钥标量运算；本模块仅覆盖摘要、认证与验签，不提供随机数、私钥生成或密钥存储。
- **空输入与并发**：SHA-256 与 HMAC-SHA-256 在长度为 `0` 时允许数据或密钥指针为
  `MR_NULL`；所有算法均无全局可变状态，不同上下文对象可安全并发，同一上下文对象不可并发修改。

## API 速查

| 分组         | API                                                          | 说明                                                  |
|--------------|--------------------------------------------------------------|-------------------------------------------------------|
| SHA-256      | `mr_sha256_init(sha256)`                                     | 初始化 SHA-256 分段上下文                             |
| SHA-256      | `mr_sha256_update(sha256, data, size)`                       | 追加任意长度输入数据                                  |
| SHA-256      | `mr_sha256_final(sha256, digest)`                            | 输出 32 字节摘要并清零上下文                          |
| SHA-256      | `mr_sha256(data, size, digest)`                              | 一次性计算完整缓冲区摘要                              |
| HMAC-SHA-256 | `mr_hmac_sha256_init(hmac, key, key_size)`                   | 以给定密钥初始化 HMAC 上下文                          |
| HMAC-SHA-256 | `mr_hmac_sha256_update(hmac, data, size)`                    | 追加认证数据                                          |
| HMAC-SHA-256 | `mr_hmac_sha256_final(hmac, auth)`                           | 输出 32 字节认证标签并清零上下文                      |
| HMAC-SHA-256 | `mr_hmac_sha256(key, key_size, data, size, auth)`            | 一次性计算 HMAC-SHA-256                               |
| P-256 基础   | `mr_p256_value_from_be(value, bytes)`                        | 从 32 字节大端字节序加载 256 位值                     |
| P-256 基础   | `mr_p256_scalar_valid(scalar)`                               | 校验标量是否在 `[1, n-1]` 范围内                      |
| P-256 基础   | `mr_p256_scalar_reduce(scalar)`                              | 按曲线阶 `n` 约减标量                                 |
| P-256 基础   | `mr_p256_scalar_inv(p256, out, scalar)`                      | 计算标量模逆                                          |
| P-256 基础   | `mr_p256_scalar_mul(p256, out, a, b)`                        | 计算标量模乘                                          |
| P-256 基础   | `mr_p256_point_from_public(point, public_key)`               | 将公钥坐标载入仿射点                                  |
| P-256 基础   | `mr_p256_point_valid(p256, point)`                           | 校验点是否在 P-256 曲线上且非无穷远点                 |
| P-256 基础   | `mr_p256_point_mul_generator(p256, out, scalar)`             | 计算生成元标量乘 `scalar * G`                         |
| P-256 基础   | `mr_p256_point_mul(p256, out, point, scalar)`                | 计算任意点标量乘                                      |
| P-256 基础   | `mr_p256_point_add(p256, out, a, b)`                         | 计算两点相加                                          |
| ECDSA P-256  | `mr_ecdsa_p256_verify(ecdsa, public_key, digest, signature)` | 验证 32 字节摘要的原始 `r/s` 签名，有效返回 `MR_TRUE` |

## 内部实现

- SHA-256（`modules/crypto/sha/sha256.c`）采用 64 字节分组缓冲与压缩函数；HMAC-SHA-256（
  `modules/crypto/sha/hmac_sha256.c`）在上下文中保存 `inner` 与 `outer` 两个
  SHA-256 状态。
- P-256（`modules/crypto/ecc/p256.c`）以 8 个 32 位字表示大整数，点倍加与标量乘在
  Jacobian 射影坐标下完成并在输出前转回仿射坐标；ECDSA 验签（
  `modules/crypto/ecc/ecdsa_p256.c`）完全通过 P-256 公开 API 组合实现。
