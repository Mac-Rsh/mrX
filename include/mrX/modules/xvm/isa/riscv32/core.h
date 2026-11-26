/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __RISCV32_CORE_H__
#define __RISCV32_CORE_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_XVM_RISCV32)
#include <mrX/modules/xvm/isa/riscv32/instr.h>
#include <mrX/modules/xvm/isa/riscv32/mmio.h>
#include <mrX/modules/xvm/mr_xvm.h>
#include <mrX/libc/mr_string.h>
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup XVM-RISCV32
 * @{
 */

#if defined(MR_USE_MODULE_XVM_RISCV32)
struct mr_xvm;

/* RISCV32 CSR definition */
#define REG_CSR_MSTATUS                 (0x300)
#define REG_CSR_MISA                    (0x301)
#define REG_CSR_MIE                     (0x304)
#define REG_CSR_MTVEC                   (0x305)
#define REG_CSR_MSCRATCH                (0x340)
#define REG_CSR_MEPC                    (0x341)
#define REG_CSR_MCAUSE                  (0x342)
#define REG_CSR_MTVAL                   (0x343)
#define REG_CSR_MIP                     (0x344)
#define REG_CSR_MCYCLE                  (0xb00)
#define REG_CSR_MINSTRET                (0xb02)
#define REG_CSR_MCYCLEH                 (0xb80)
#define REG_CSR_MINSTRETH               (0xb82)
#define REG_CSR_CYCLE                   (0xc00)
#define REG_CSR_INSTRET                 (0xc02)
#define REG_CSR_CYCLEH                  (0xc80)
#define REG_CSR_INSTRETH                (0xc82)
#define REG_CSR_MVENDORID               (0xf11)
#define REG_CSR_MARCHID                 (0xf12)
#define REG_CSR_MIMPID                  (0xf13)
#define REG_CSR_MHARTID                 (0xf14)

/* RISCV32 MSTATUS definition */
#define REG_MSTATUS_MIE                 (1u << 3)
#define REG_MSTATUS_MPIE                (1u << 7)
#define REG_MSTATUS_MPP_MASK            (3u << 11)
#define REG_MSTATUS_MPP_M               (3u << 11)

/* RISCV32 MIP/MIE definition */
#define REG_MIP_MSIP                    (1u << 3)
#define REG_MIP_MTIP                    (1u << 7)
#define REG_MIP_MEIP                    (1u << 11)
#define REG_MIE_MSIE                    (1u << 3)
#define REG_MIE_MTIE                    (1u << 7)
#define REG_MIE_MEIE                    (1u << 11)

/* RISCV32 MCAUSE definition */
#define REG_MCAUSE_INTR                 (1u << 31)
#define REG_MCAUSE_MSI                  (3u)
#define REG_MCAUSE_MTI                  (7u)
#define REG_MCAUSE_MEI                  (11u)
#define REG_MCAUSE_MSI_INTR             (REG_MCAUSE_INTR | REG_MCAUSE_MSI)
#define REG_MCAUSE_MTI_INTR             (REG_MCAUSE_INTR | REG_MCAUSE_MTI)
#define REG_MCAUSE_MEI_INTR             (REG_MCAUSE_INTR | REG_MCAUSE_MEI)

/* RISCV32 trap type */
typedef enum trap_type {
    TRAP_TYPE_NONE,
    TRAP_TYPE_SYSCALL,
} trap_type_t;

/* RISCV32 core registers type */
typedef struct core_regs {
    mr_uint32_t x[32];
    mr_uint32_t pc;
    mr_uint32_t mstatus;
    mr_uint32_t misa;
    mr_uint32_t mie;
    mr_uint32_t mtvec;
    mr_uint32_t mscratch;
    mr_uint32_t mepc;
    mr_uint32_t mcause;
    mr_uint32_t mtval;
    mr_uint32_t mip;
    mr_uint64_t mcycle;
    mr_uint64_t minstret;
} core_regs_t;

/* RISCV32 syscall shadow type */
typedef struct syscall_shadow {
    mr_uint64_t param[MR_CFG_XVM_SYSCALL_PARAM_NUM];
    mr_uint64_t ret;
    mr_bool_t pending;
} syscall_shadow_t;

/* RISCV32 PLIC interrupt controller type */
typedef struct plic_control {
    mr_atomic_t level;
    mr_atomic_t pending;
    mr_uint32_t enable;
    mr_uint32_t claimed;
    mr_uint32_t threshold;
    mr_uint8_t priority[CORE_MMIO_PLIC_IRQ_NUM];
} plic_control_t;

/* RISCV32 CLINT interrupt controller type */
typedef struct clint_control {
    mr_uint32_t msip;
    mr_uint64_t mtimecmp;
} clint_control_t;

#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL)
#define CORE_LOOP_MISS_FILTER_SIZE      (8U)
#if !defined(MR_CFG_XVM_RISCV32_LOOP_CACHE_SIZE)
#define MR_CFG_XVM_RISCV32_LOOP_CACHE_SIZE (32U)
#endif /* !defined(MR_CFG_XVM_RISCV32_LOOP_CACHE_SIZE) */
#if !defined(MR_CFG_XVM_RISCV32_LOOP_MISS_SIZE)
#define MR_CFG_XVM_RISCV32_LOOP_MISS_SIZE  (4U)
#endif /* !defined(MR_CFG_XVM_RISCV32_LOOP_MISS_SIZE) */
#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED)
#if !defined(MR_CFG_XVM_RISCV32_LOOP_POOL_SIZE)
#define MR_CFG_XVM_RISCV32_LOOP_POOL_SIZE  (32U)
#endif /* !defined(MR_CFG_XVM_RISCV32_LOOP_POOL_SIZE) */
#if !defined(MR_CFG_XVM_RISCV32_LOOP_ARENA_SIZE)
#define MR_CFG_XVM_RISCV32_LOOP_ARENA_SIZE (256U)
#endif /* !defined(MR_CFG_XVM_RISCV32_LOOP_ARENA_SIZE) */
#else
#if !defined(MR_CFG_XVM_RISCV32_LOOP_PRIVATE_SLOTS)
#define MR_CFG_XVM_RISCV32_LOOP_PRIVATE_SLOTS (1U)
#endif /* !defined(MR_CFG_XVM_RISCV32_LOOP_PRIVATE_SLOTS) */
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED) */

/* Recognized tight loop kinds */
typedef enum loop_kind {
    LOOP_KIND_NONE,
    LOOP_KIND_DELAY,
    LOOP_KIND_FILL,
    LOOP_KIND_SUM,
    LOOP_KIND_COPY,
    LOOP_KIND_DECODED,
} loop_kind_t;

/* Pre-decoded loop micro-opcodes */
typedef enum loop_uop_op {
    UOP_NOP,
    UOP_ADD,
    UOP_SUB,
    UOP_SLL,
    UOP_SLT,
    UOP_SLTU,
    UOP_XOR,
    UOP_SRL,
    UOP_SRA,
    UOP_OR,
    UOP_AND,
    UOP_MUL,
    UOP_MULH,
    UOP_MULHSU,
    UOP_MULHU,
    UOP_DIV,
    UOP_DIVU,
    UOP_REM,
    UOP_REMU,
    UOP_ADDI,
    UOP_SLLI,
    UOP_SLTI,
    UOP_SLTIU,
    UOP_XORI,
    UOP_SRLI,
    UOP_SRAI,
    UOP_ORI,
    UOP_ANDI,
    UOP_LW,
    UOP_LOAD,
    UOP_SW,
    UOP_STORE,
    UOP_LUI,
    UOP_BR,
} loop_uop_op_t;

/* Pre-decoded loop micro-op (8 bytes, zero padding) */
typedef struct loop_uop {
    mr_uint32_t imm;
    mr_uint8_t op;
    mr_uint8_t rd;
    mr_uint8_t rs1;
    mr_uint8_t rs2;
} loop_uop_t;

/* Compact decoded loop descriptor (36 bytes) */
typedef struct loop_desc {
    mr_uint32_t branch_pc;
    mr_uint32_t loop_start;
    mr_uint32_t branch_next;
    union {
        struct {
            mr_int32_t off0;
            mr_int32_t off1;
            mr_uint8_t width_shift;
            mr_uint8_t load_funct3;
            mr_uint8_t r2;
            mr_uint8_t r3;
            mr_uint8_t cmp_reg;
        } idiom;
        struct {
            mr_uint32_t callee_pc;
            mr_uint16_t uop_start;
            mr_uint8_t body_count;
            mr_uint8_t callee_start;
            mr_uint8_t callee_end;
            mr_uint8_t elided_call;
            mr_uint8_t elided_ret;
#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED)
            mr_uint8_t has_exit;
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED) */
        } dec;
    } u;
    mr_uint8_t kind;
    mr_uint8_t instr_count;
    mr_uint8_t branch_funct3;
    mr_uint8_t r0;
    mr_uint8_t r1;
} loop_desc_t;

#if !defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED)
/* Private decoded loop cache slot */
typedef struct loop_private_slot {
    loop_desc_t desc;
    loop_uop_t uops[MR_CFG_XVM_RISCV32_LOOP_CACHE_SIZE - 1U];
    mr_uint8_t offs[MR_CFG_XVM_RISCV32_LOOP_CACHE_SIZE - 1U];
} loop_private_slot_t;
#endif /* !defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED) */
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL) */

/* RISCV32 core type */
typedef struct core {
#if defined(MR_USE_MODULE_XVM_MMIO)
    struct mr_xvm *xvm;
#endif /* defined(MR_USE_MODULE_XVM_MMIO) */
    const mr_xvm_binary_t *bin;
    syscall_shadow_t syscall;
    mr_uint32_t trap_mtval;
    volatile mr_uint32_t irq_dirty;
    mr_uint32_t mem_base;
    plic_control_t plic;
    clint_control_t clint;
    core_regs_t regs;
#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL)
    mr_uint32_t miss_pc[CORE_LOOP_MISS_FILTER_SIZE];
#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED)
    mr_uint32_t image_hash;
    mr_uint32_t low_yield_pc;
    mr_uint8_t low_yield_cnt;
    mr_uint8_t low_yield_skip;
#else
    loop_private_slot_t loop_slots[MR_CFG_XVM_RISCV32_LOOP_PRIVATE_SLOTS];
    mr_uint32_t loop_miss_pc[MR_CFG_XVM_RISCV32_LOOP_MISS_SIZE];
    mr_uint8_t loop_miss_idx;
#if (MR_CFG_XVM_RISCV32_LOOP_PRIVATE_SLOTS > 1U)
    mr_uint8_t loop_slot_next;
#endif /* (MR_CFG_XVM_RISCV32_LOOP_PRIVATE_SLOTS > 1U) */
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED) */
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL) */
    mr_size_t mem_size;
    mr_uint8_t mem[];
} core_t;

/* RISCV32 ISA feature defaults */
#if defined(MR_USE_MODULE_XVM_RISCV32_C)
#define CORE_INSTR_ALIGN                (2U)
#define CORE_MISA_DEFAULT               (0x40001104U)
#else
#define CORE_INSTR_ALIGN                (4U)
#define CORE_MISA_DEFAULT               (0x40001100U)
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_C) */

/* RISCV32 core registers macro function definition */
#define CORE_REG(_core)                 ((_core)->regs)
#define CORE_REG_X(_core, _reg)         (CORE_REG(_core).x[(_reg)])
#define CORE_REG_PC(_core)              (CORE_REG(_core).pc)
#define CORE_REG_MSTATUS(_core)         (CORE_REG(_core).mstatus)
#define CORE_REG_MISA(_core)            (CORE_REG(_core).misa)
#define CORE_REG_MIE(_core)             (CORE_REG(_core).mie)
#define CORE_REG_MTVEC(_core)           (CORE_REG(_core).mtvec)
#define CORE_REG_MSCRATCH(_core)        (CORE_REG(_core).mscratch)
#define CORE_REG_MEPC(_core)            (CORE_REG(_core).mepc)
#define CORE_REG_MCAUSE(_core)          (CORE_REG(_core).mcause)
#define CORE_REG_MTVAL(_core)           (CORE_REG(_core).mtval)
#define CORE_REG_MIP(_core)             (CORE_REG(_core).mip)
#define CORE_REG_MCYCLE(_core)          (CORE_REG(_core).mcycle)
#define CORE_REG_MINSTRET(_core)        (CORE_REG(_core).minstret)

/* RISCV32 core address macro function definition */
#define CORE_ADDR_IS_ALIGN(_addr, _size)                                       \
    (((_addr) & ((_size) - 1)) == 0)
#define CORE_ADDR_BIN(_core, _addr)     (_addr)
#define CORE_ADDR_MEM(_core, _addr)     ((_addr) - (_core)->mem_base)
#define CORE_ADDR_IS_IN_BIN(_core, _addr, _size)                               \
    (((_core)->bin->size >= (_size))                                           \
     && ((_addr) <= ((_core)->bin->size - (_size))))
#define CORE_ADDR_IS_IN_MEM(_core, _addr, _size)                               \
    (((_core)->mem_size >= (_size))                                            \
     && ((_addr) <= ((_core)->mem_size - (_size))))
#define CORE_ADDR_IS_IN_MMIO(_core, _addr, _size)                              \
    ((CORE_MMIO_SIZE >= (_size)) && ((_addr) >= CORE_MMIO_BASE)                \
     && ((_addr) <= (CORE_MMIO_END - (_size))))

/* Native little-endian unaligned memcpy builtin detection */
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#if defined(__has_builtin)
#if __has_builtin(__builtin_memcpy)
#define CORE_USE_BUILTIN_MEMCPY         (1)
#endif /* __has_builtin(__builtin_memcpy) */
#elif defined(__GNUC__)
#define CORE_USE_BUILTIN_MEMCPY         (1)
#endif /* defined(__has_builtin) */
#endif /* defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) */

/**
 * @brief This function loads an 8-bit value from memory.
 *
 * @param memory The source memory pointer.
 * @return The loaded value.
 */
MR_INLINE mr_uint8_t core_load8(const mr_uint8_t *memory) {
    return memory[0];
}

/**
 * @brief This function loads an unaligned little-endian 16-bit value.
 *
 * @param memory The source memory pointer.
 * @return The loaded value.
 */
MR_INLINE mr_uint16_t core_load16(const mr_uint8_t *memory) {
#if defined(CORE_USE_BUILTIN_MEMCPY)
    mr_uint16_t value;

    __builtin_memcpy(&value, memory, sizeof(value));
    return value;
#else
    return (mr_uint16_t)memory[0] | ((mr_uint16_t)memory[1] << 8);
#endif /* defined(CORE_USE_BUILTIN_MEMCPY) */
}

/**
 * @brief This function loads an unaligned little-endian 32-bit value.
 *
 * @param memory The source memory pointer.
 * @return The loaded value.
 */
MR_INLINE mr_uint32_t core_load32(const mr_uint8_t *memory) {
#if defined(CORE_USE_BUILTIN_MEMCPY)
    mr_uint32_t value;

    __builtin_memcpy(&value, memory, sizeof(value));
    return value;
#else
    return (mr_uint32_t)memory[0] | ((mr_uint32_t)memory[1] << 8)
         | ((mr_uint32_t)memory[2] << 16) | ((mr_uint32_t)memory[3] << 24);
#endif /* defined(CORE_USE_BUILTIN_MEMCPY) */
}

/**
 * @brief This function stores an 8-bit value to memory.
 *
 * @param memory The destination memory pointer.
 * @param value The value to store.
 */
MR_INLINE void core_store8(mr_uint8_t *memory, mr_uint32_t value) {
    memory[0] = (mr_uint8_t)value;
}

/**
 * @brief This function stores an unaligned little-endian 16-bit value.
 *
 * @param memory The destination memory pointer.
 * @param value The value to store.
 */
MR_INLINE void core_store16(mr_uint8_t *memory, mr_uint32_t value) {
#if defined(CORE_USE_BUILTIN_MEMCPY)
    mr_uint16_t raw;

    raw = (mr_uint16_t)value;
    __builtin_memcpy(memory, &raw, sizeof(raw));
#else
    memory[0] = (mr_uint8_t)value;
    memory[1] = (mr_uint8_t)(value >> 8);
#endif /* defined(CORE_USE_BUILTIN_MEMCPY) */
}

/**
 * @brief This function stores an unaligned little-endian 32-bit value.
 *
 * @param memory The destination memory pointer.
 * @param value The value to store.
 */
MR_INLINE void core_store32(mr_uint8_t *memory, mr_uint32_t value) {
#if defined(CORE_USE_BUILTIN_MEMCPY)
    __builtin_memcpy(memory, &value, sizeof(value));
#else
    memory[0] = (mr_uint8_t)value;
    memory[1] = (mr_uint8_t)(value >> 8);
    memory[2] = (mr_uint8_t)(value >> 16);
    memory[3] = (mr_uint8_t)(value >> 24);
#endif /* defined(CORE_USE_BUILTIN_MEMCPY) */
}

/**
 * @brief This function performs a portable 32-bit arithmetic right shift.
 *
 * @param val The input 32-bit value.
 * @param shamt The shift amount(only low 5 bits are used).
 * @return The shifted 32-bit value.
 */
MR_INLINE mr_uint32_t core_sra32(mr_uint32_t val, mr_uint32_t shamt) {
    shamt &= 0x1fU;
    if ((shamt == 0U) || ((val & 0x80000000U) == 0U)) {
        return val >> shamt;
    }
    return (val >> shamt) | (~0U << (32U - shamt));
}
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __RISCV32_CORE_H__ */
