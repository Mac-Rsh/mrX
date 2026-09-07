/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-16        Codex        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_MODULE_XVM_RISCV32)
#include <mrX/libc/mr_string.h>
#include <mrX/modules/xvm/mr_xvm.h>
#include <mrX/modules/xvm/isa/riscv32/core.h>

/* Constants */

#define TEST_XVM_DATA_SIZE (512U)
#define TEST_XVM_ENTRY     (0x100U)

/* Helpers */

static void test_xvm_image_word(mr_uint8_t *image, mr_uint32_t index,
                                mr_uint32_t value) {
    mr_memcpy(image + (index * sizeof(value)), &value, sizeof(value));
}

static mr_err_t test_xvm_prepare(mr_xvm_t *xvm, mr_uint8_t *data,
                                 const mr_uint8_t *image, mr_size_t size) {
    mr_err_t ret;

    /* Initialize XVM */
    ret = mr_xvm_init(xvm, "riscv32", data, TEST_XVM_DATA_SIZE);
    if (ret != 0) {
        return ret;
    }

    /* Load image */
    ret = mr_xvm_load(xvm, image, size, TEST_XVM_ENTRY);
    if (ret != 0) {
        mr_xvm_del(xvm);
        return ret;
    }

    /* Set entry program counter */
    return mr_xvm_reg_store(xvm, 32, 0);
}

static const mr_xvm_isa_ops_t test_xvm_empty_ops = {0};

MR_TEST(xvm, invalid_lifecycle_and_isa_registry) {
    mr_uint8_t data[TEST_XVM_DATA_SIZE];
    mr_xvm_t xvm;
    mr_xvm_isa_t isa
        = MR_XVM_ISA_INIT("test-empty", &test_xvm_empty_ops, MR_NULL);
    mr_xvm_isa_t *found;
    mr_err_t ret;

    /* Unknown ISA and zero-sized allocation are rejected. */
    ret = mr_xvm_init(&xvm, "missing", data, sizeof(data));
    MR_TEST_ASSERT_EQ_INT(ret, -MR_ENOENT);
    MR_TEST_ASSERT_EQ_PTR(mr_xvm_create("missing", sizeof(data)), MR_NULL);

    /* Registration keeps the static ISA alive and rejects duplicates. */
    ret = mr_xvm_isa_register(&isa);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    found = mr_xvm_isa_find("test-empty");
    MR_TEST_ASSERT_EQ_PTR(found, &isa);
    if (found != MR_NULL) {
        mr_xvm_isa_put(found);
    }
    ret = mr_xvm_isa_register(&isa);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EEXIST);
    ret = mr_xvm_isa_del(&isa);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EBUSY);
    ret = mr_xvm_isa_unregister(&isa);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_xvm_isa_unregister(&isa);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_ENOENT);
    ret = mr_xvm_isa_del(&isa);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

/* Tests */

MR_TEST(xvm, lifecycle_and_registers) {
    mr_uint8_t data[TEST_XVM_DATA_SIZE];
    mr_uint8_t image[4] = {0};
    mr_xvm_t xvm;
    mr_uint64_t value;
    mr_err_t ret;

    ret = test_xvm_prepare(&xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_status(&xvm), MR_XVM_STATUS_RUNNING);

    ret = mr_xvm_reg_store(&xvm, 0, 0x1234);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_xvm_reg_load(&xvm, 0, &value);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0);

    ret = mr_xvm_reg_store(&xvm, 1, 0x12345678U);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_xvm_reg_load(&xvm, 1, &value);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0x12345678U);

    ret = mr_xvm_reg_load(&xvm, 0xFFFFFFFFU, &value);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EINVAL);

    ret = mr_xvm_del(&xvm);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(xvm, execute_arithmetic_and_syscall) {
    mr_uint8_t data[TEST_XVM_DATA_SIZE];
    mr_uint8_t image[12] = {0};
    mr_xvm_event_t event = {0};
    mr_xvm_t xvm;
    mr_uint64_t value;
    mr_err_t ret;

    /* ADDI x1, x0, 5; ADDI x2, x1, 7; ECALL */
    test_xvm_image_word(image, 0, 0x00500093U);
    test_xvm_image_word(image, 1, 0x00708113U);
    test_xvm_image_word(image, 2, 0x00000073U);

    ret = test_xvm_prepare(&xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_xvm_reg_store(&xvm, 17, 1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_xvm_reg_store(&xvm, 10, 9);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 2), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_UINT(event.none.step, 2);
    ret = mr_xvm_reg_load(&xvm, 2, &value);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_UINT(value, 12);

    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 1), MR_XVM_SYSCALL);
    MR_TEST_ASSERT_EQ_UINT(event.syscall.nr, 1);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_status(&xvm), MR_XVM_STATUS_PAUSED);
    MR_TEST_ASSERT_EQ_UINT(*event.syscall.param[0], 9);

    /* Write the syscall result back before resuming execution. */
    *event.syscall.ret = 42;
    ret = mr_xvm_resume(&xvm);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    (void)mr_xvm_execute(&xvm, &event, 0);
    ret = mr_xvm_reg_load(&xvm, 10, &value);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_UINT(value, 42);

    ret = mr_xvm_del(&xvm);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(xvm, memory_permissions_and_bounds) {
    mr_uint8_t data[TEST_XVM_DATA_SIZE];
    mr_uint8_t image[4] = {0x13, 0x00, 0x00, 0x00};
    mr_uint8_t source[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    mr_uint8_t target[8] = {0};
    mr_xvm_t xvm;
    mr_ssize_t size;
    mr_err_t ret;

    ret = test_xvm_prepare(&xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    size = mr_xvm_datamem_store(&xvm, TEST_XVM_ENTRY, source, sizeof(source));
    MR_TEST_ASSERT_EQ_INT(size, sizeof(source));
    size = mr_xvm_datamem_load(&xvm, TEST_XVM_ENTRY, target, sizeof(target));
    MR_TEST_ASSERT_EQ_INT(size, sizeof(target));
    MR_TEST_EXPECT_EQ_INT(mr_memcmp(source, target, sizeof(source)), 0);

    size = mr_xvm_datamem_store(&xvm, 0, source, sizeof(source));
    MR_TEST_ASSERT_EQ_INT(size, -MR_EPERM);
    size = mr_xvm_datamem_load(&xvm, TEST_XVM_ENTRY + TEST_XVM_DATA_SIZE,
                               target, sizeof(target));
    MR_TEST_ASSERT_EQ_INT(size, -MR_EINVAL);
    size = mr_xvm_datamem_load(&xvm, 0x100000000ULL, target, sizeof(target));
    MR_TEST_ASSERT_EQ_INT(size, -MR_EINVAL);

    ret = mr_xvm_del(&xvm);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(xvm, exception_pause_resume_and_interrupt) {
    mr_uint8_t data[TEST_XVM_DATA_SIZE];
    mr_uint8_t image[4] = {0xff, 0xff, 0xff, 0xff};
    mr_xvm_event_t event = {0};
    mr_xvm_t xvm;
    mr_err_t ret;

    ret = test_xvm_prepare(&xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_xvm_pause(&xvm);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_status(&xvm), MR_XVM_STATUS_PAUSED);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 1), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_UINT(event.none.step, 0);

    ret = mr_xvm_resume(&xvm);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_status(&xvm), MR_XVM_STATUS_RUNNING);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_interrupt(&xvm, 32, MR_TRUE), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_interrupt(&xvm, 1, MR_TRUE), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_interrupt(&xvm, 1, MR_FALSE), 0);

    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 1), MR_XVM_EXCEPT);
    MR_TEST_ASSERT_EQ_UINT(event.except.nr, MR_XVM_EXCEPT_NR_ILLEGAL);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_status(&xvm), MR_XVM_STATUS_EXCEPT);

    ret = mr_xvm_del(&xvm);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

/* Instruction encoders */

static mr_uint32_t test_xvm_r(mr_uint32_t funct7, mr_uint32_t rs2,
                              mr_uint32_t rs1, mr_uint32_t funct3,
                              mr_uint32_t rd) {
    return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12)
         | (rd << 7) | 0x33U;
}

static mr_uint32_t test_xvm_i(mr_uint32_t imm, mr_uint32_t rs1,
                              mr_uint32_t funct3, mr_uint32_t rd,
                              mr_uint32_t opcode) {
    return ((imm & 0xfffU) << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7)
         | opcode;
}

static mr_uint32_t test_xvm_s(mr_uint32_t imm, mr_uint32_t rs2, mr_uint32_t rs1,
                              mr_uint32_t funct3) {
    return (((imm >> 5) & 0x7fU) << 25) | (rs2 << 20) | (rs1 << 15)
         | (funct3 << 12) | ((imm & 0x1fU) << 7) | 0x23U;
}

static mr_uint32_t test_xvm_b(mr_uint32_t imm, mr_uint32_t rs2, mr_uint32_t rs1,
                              mr_uint32_t funct3) {
    return (((imm >> 12) & 1U) << 31) | (((imm >> 5) & 0x3fU) << 25)
         | (rs2 << 20) | (rs1 << 15) | (funct3 << 12)
         | (((imm >> 1) & 0xfU) << 8) | (((imm >> 11) & 1U) << 7) | 0x63U;
}

static mr_uint32_t test_xvm_u(mr_uint32_t imm, mr_uint32_t rd,
                              mr_uint32_t opcode) {
    return (imm & 0xfffff000U) | (rd << 7) | opcode;
}

static mr_uint32_t test_xvm_j(mr_uint32_t imm, mr_uint32_t rd) {
    return (((imm >> 20) & 1U) << 31) | (((imm >> 1) & 0x3ffU) << 21)
         | (((imm >> 11) & 1U) << 20) | (((imm >> 12) & 0xffU) << 12)
         | (rd << 7) | 0x6fU;
}

MR_TEST(xvm, riscv32_integer_and_control_flow) {
    mr_uint8_t image[52] = {0};
    mr_uint8_t data[TEST_XVM_DATA_SIZE];
    mr_xvm_event_t event = {0};
    mr_xvm_t xvm;
    mr_uint64_t value;
    mr_err_t ret;

    /* Exercise RV32I arithmetic, shifts, comparisons and branches. */
    test_xvm_image_word(image, 0, test_xvm_i(5, 0, 0, 1, 0x13));
    test_xvm_image_word(image, 1, test_xvm_i(3, 0, 0, 2, 0x13));
    test_xvm_image_word(image, 2, test_xvm_r(0, 2, 1, 0, 3));
    test_xvm_image_word(image, 3, test_xvm_r(0x20, 2, 1, 0, 4));
    test_xvm_image_word(image, 4, test_xvm_r(0, 2, 1, 7, 5));
    test_xvm_image_word(image, 5, test_xvm_r(0, 2, 1, 6, 6));
    test_xvm_image_word(image, 6, test_xvm_i(1, 1, 1, 7, 0x13));
    test_xvm_image_word(image, 7, test_xvm_i(1, 1, 5, 8, 0x13));
    test_xvm_image_word(image, 8, test_xvm_i(0x401, 4, 5, 9, 0x13));
    test_xvm_image_word(image, 9, test_xvm_b(8, 1, 1, 0));
    test_xvm_image_word(image, 10, test_xvm_i(99, 0, 0, 10, 0x13));
    test_xvm_image_word(image, 11, test_xvm_i(7, 0, 0, 10, 0x13));
    test_xvm_image_word(image, 12, test_xvm_u(0x12345000, 11, 0x37));
    ret = test_xvm_prepare(&xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 12), MR_XVM_NONE);

    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 3, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 8);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 4, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 2);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 5, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 1);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 6, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 7);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 10, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 7);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 11, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0x12345000U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);
}

MR_TEST(xvm, riscv32_load_store_and_faults) {
    mr_uint8_t image[44] = {0};
    mr_uint8_t data[TEST_XVM_DATA_SIZE];
    mr_xvm_event_t event = {0};
    mr_xvm_t xvm;
    mr_uint64_t value;
    mr_err_t ret;

    /* Store all supported widths, then test signed and unsigned loads. */
    test_xvm_image_word(image, 0, test_xvm_i(0x100, 0, 0, 1, 0x13));
    test_xvm_image_word(image, 1, test_xvm_i(0xf80, 0, 0, 2, 0x13));
    test_xvm_image_word(image, 2, test_xvm_s(0, 2, 1, 0));
    test_xvm_image_word(image, 3, test_xvm_s(2, 2, 1, 1));
    test_xvm_image_word(image, 4, test_xvm_s(4, 2, 1, 2));
    test_xvm_image_word(image, 5, test_xvm_i(0, 1, 0, 3, 0x03));
    test_xvm_image_word(image, 6, test_xvm_i(0, 1, 4, 4, 0x03));
    test_xvm_image_word(image, 7, test_xvm_i(2, 1, 1, 5, 0x03));
    test_xvm_image_word(image, 8, test_xvm_i(2, 1, 5, 6, 0x03));
    test_xvm_image_word(image, 9, test_xvm_i(4, 1, 2, 7, 0x03));
    test_xvm_image_word(image, 10, 0x00000013U);

    ret = test_xvm_prepare(&xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 11), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 3, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0xffffff80U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 4, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0x80U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 5, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0xffffff80U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 6, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0xff80U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 7, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0xffffff80U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);

    /* Misaligned load must be reported as an alignment exception. */
    test_xvm_image_word(image, 0, test_xvm_i(0x101, 0, 0, 1, 0x13));
    test_xvm_image_word(image, 1, test_xvm_i(0, 1, 1, 2, 0x03));
    ret = test_xvm_prepare(&xvm, data, image, 8);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 2), MR_XVM_EXCEPT);
    MR_TEST_ASSERT_EQ_UINT(event.except.nr, MR_XVM_EXCEPT_NR_ALIGN);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);
}

MR_TEST(xvm, riscv32_csr_counter_and_mret) {
    mr_uint8_t image[56] = {0};
    mr_uint8_t data[TEST_XVM_DATA_SIZE];
    mr_xvm_event_t event = {0};
    mr_xvm_t xvm;
    mr_uint64_t value;
    mr_err_t ret;

    /* CSR read/write/set/clear and the machine return path. */
    test_xvm_image_word(image, 0, test_xvm_i(8, 0, 0, 1, 0x13));
    test_xvm_image_word(image, 1, test_xvm_i(REG_CSR_MSTATUS, 1, 1, 2, 0x73));
    test_xvm_image_word(image, 2, test_xvm_i(REG_CSR_MSTATUS, 1, 2, 3, 0x73));
    test_xvm_image_word(image, 3, test_xvm_i(REG_CSR_MSTATUS, 1, 3, 4, 0x73));
    test_xvm_image_word(image, 4, test_xvm_i(REG_CSR_MCYCLE, 0, 2, 5, 0x73));
    test_xvm_image_word(image, 5, 0x30200073U);

    ret = test_xvm_prepare(&xvm, data, image, 24);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 5), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 2, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 3, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 8);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 4, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 8);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 5, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 4);

    /* CSR numbers are not in the target register table; write them in-code */
    test_xvm_image_word(image, 5, test_xvm_i(0x30, 0, 0, 6, 0x13));
    test_xvm_image_word(image, 6, test_xvm_i(REG_CSR_MEPC, 6, 1, 0, 0x73));
    test_xvm_image_word(image, 7, test_xvm_i(REG_MSTATUS_MPIE, 0, 0, 6, 0x13));
    test_xvm_image_word(image, 8, test_xvm_i(REG_CSR_MSTATUS, 6, 1, 0, 0x73));
    test_xvm_image_word(image, 9, 0x30200073U);
    test_xvm_image_word(image, 10, 0x00000013U);
    test_xvm_image_word(image, 11, 0x00000013U);
    test_xvm_image_word(image, 12, test_xvm_i(0x5a, 0, 0, 7, 0x13));
    ret = mr_xvm_load(&xvm, image, sizeof(image), TEST_XVM_ENTRY);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_xvm_reg_store(&xvm, 32, 20);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 6), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 32, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0x34);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 7, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0x5a);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);
}

MR_TEST(xvm, riscv32_jump_and_external_interrupt) {
    mr_uint8_t image[80] = {0};
    mr_uint8_t data[TEST_XVM_DATA_SIZE];
    mr_xvm_event_t event = {0};
    mr_xvm_t xvm;
    mr_uint64_t value;
    mr_err_t ret;

    /* JAL skips one instruction and records the return address. */
    test_xvm_image_word(image, 0, test_xvm_j(8, 1));
    test_xvm_image_word(image, 1, test_xvm_i(99, 0, 0, 2, 0x13));
    test_xvm_image_word(image, 2, test_xvm_i(7, 0, 0, 3, 0x13));
    ret = test_xvm_prepare(&xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 2), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 1, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 4);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 2, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 3, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 7);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);

    /* An enabled external interrupt enters the machine trap vector. */
    test_xvm_image_word(image, 0, test_xvm_i(0x21, 0, 0, 6, 0x13));
    test_xvm_image_word(image, 1, test_xvm_i(REG_CSR_MTVEC, 6, 1, 0, 0x73));
    test_xvm_image_word(image, 2, test_xvm_i(REG_MSTATUS_MIE, 0, 0, 6, 0x13));
    test_xvm_image_word(image, 3, test_xvm_i(REG_CSR_MSTATUS, 6, 1, 0, 0x73));
    test_xvm_image_word(image, 4, test_xvm_i(REG_MIE_MEIE, 0, 0, 6, 0x13));
    test_xvm_image_word(image, 5, test_xvm_i(REG_CSR_MIE, 6, 1, 0, 0x73));
    test_xvm_image_word(image, 6, test_xvm_u(CORE_MMIO_BASE, 1, 0x37));
    test_xvm_image_word(image, 7, test_xvm_i(REG_MIE_MEIE, 0, 0, 2, 0x13));
    test_xvm_image_word(image, 8, test_xvm_s(4, 2, 1, 2));
    test_xvm_image_word(image, 9, 0x00000013U);
    test_xvm_image_word(image, 10, 0x00000013U);
    test_xvm_image_word(image, 11, 0x00000013U);
    test_xvm_image_word(image, 12, 0x00000013U);
    test_xvm_image_word(image, 13, 0x00000013U);
    test_xvm_image_word(image, 14, 0x00000013U);
    test_xvm_image_word(image, 15, 0x00000013U);
    test_xvm_image_word(image, 16, 0x00000013U);
    test_xvm_image_word(image, 17, 0x00000013U);
    test_xvm_image_word(image, 18, 0x00000013U);
    test_xvm_image_word(image, 19, test_xvm_i(REG_CSR_MCAUSE, 0, 2, 5, 0x73));
    ret = test_xvm_prepare(&xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 9), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_interrupt(&xvm, 11, MR_TRUE), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 1), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 32, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0x4c);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 1), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 5, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, REG_MCAUSE_MEI_INTR);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);
}

MR_TEST(xvm, riscv32_mul_div_and_system) {
    mr_uint8_t image[68] = {0};
    mr_uint8_t data[TEST_XVM_DATA_SIZE];
    mr_xvm_event_t event = {0};
    mr_xvm_t xvm;
    mr_uint64_t value;
    mr_err_t ret;

    /* Exercise all M-extension operations and their divide-by-zero paths. */
    test_xvm_image_word(image, 0, test_xvm_i(20, 0, 0, 1, 0x13));
    test_xvm_image_word(image, 1, test_xvm_i(3, 0, 0, 2, 0x13));
    test_xvm_image_word(image, 2, test_xvm_r(1, 2, 1, 0, 3));
    test_xvm_image_word(image, 3, test_xvm_r(1, 2, 1, 1, 4));
    test_xvm_image_word(image, 4, test_xvm_r(1, 2, 1, 2, 5));
    test_xvm_image_word(image, 5, test_xvm_r(1, 2, 1, 3, 6));
    test_xvm_image_word(image, 6, test_xvm_r(1, 2, 1, 4, 7));
    test_xvm_image_word(image, 7, test_xvm_r(1, 2, 1, 5, 8));
    test_xvm_image_word(image, 8, test_xvm_r(1, 2, 1, 6, 9));
    test_xvm_image_word(image, 9, test_xvm_r(1, 2, 1, 7, 10));
    test_xvm_image_word(image, 10, test_xvm_r(1, 0, 1, 4, 11));
    test_xvm_image_word(image, 11, test_xvm_r(1, 0, 1, 5, 12));
    test_xvm_image_word(image, 12, test_xvm_r(1, 0, 1, 6, 13));
    test_xvm_image_word(image, 13, test_xvm_r(1, 0, 1, 7, 14));
    test_xvm_image_word(image, 14, test_xvm_i(1, 0, 0, 15, 0x13));
    test_xvm_image_word(image, 15, test_xvm_i(0, 15, 0, 16, 0x13));
    test_xvm_image_word(image, 16, 0x0000000fU);

    ret = test_xvm_prepare(&xvm, data, image, 68);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 17), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 3, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 60);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 7, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 6);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 11, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0xffffffffU);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 13, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 20);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 14, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 20);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);
}

MR_TEST(xvm, riscv32_upper_immediate_and_jalr) {
    mr_uint8_t image[40] = {0};
    mr_uint8_t data[TEST_XVM_DATA_SIZE];
    mr_xvm_event_t event = {0};
    mr_xvm_t xvm;
    mr_uint64_t value;
    mr_err_t ret;

    /* LUI/AUIPC and JALR with an odd target (the low bit is cleared). */
    test_xvm_image_word(image, 0, test_xvm_u(0x12345000, 1, 0x37));
    test_xvm_image_word(image, 1, test_xvm_u(0x00012000, 2, 0x17));
    test_xvm_image_word(image, 2, test_xvm_i(21, 0, 0, 3, 0x13));
    test_xvm_image_word(image, 3, test_xvm_i(1, 3, 0, 4, 0x67));
    test_xvm_image_word(image, 4, test_xvm_i(99, 0, 0, 5, 0x13));
    test_xvm_image_word(image, 5, 0x00000013U);
    test_xvm_image_word(image, 6, 0x00100073U);
    ret = test_xvm_prepare(&xvm, data, image, 28);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 6), MR_XVM_EXCEPT);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 1, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0x12345000U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 2, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0x12004U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 5, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);
}

MR_TEST(xvm, riscv32_integer_variants_and_branches) {
    mr_uint8_t image[80] = {0};
    mr_uint8_t data[TEST_XVM_DATA_SIZE];
    mr_xvm_event_t event = {0};
    mr_xvm_t xvm;
    mr_uint32_t branch[6];
    mr_uint32_t i;
    mr_uint64_t value;
    mr_err_t ret;

    /* Cover the remaining OP and OP-IMM instruction families. */
    test_xvm_image_word(image, 0, test_xvm_i(0x55, 0, 0, 1, 0x13));
    test_xvm_image_word(image, 1, test_xvm_i(0x0f, 0, 0, 2, 0x13));
    test_xvm_image_word(image, 2, test_xvm_r(0, 2, 1, 4, 3));
    test_xvm_image_word(image, 3, test_xvm_r(0, 2, 1, 6, 4));
    test_xvm_image_word(image, 4, test_xvm_r(0, 2, 1, 7, 5));
    test_xvm_image_word(image, 5, test_xvm_r(0, 2, 1, 1, 6));
    test_xvm_image_word(image, 6, test_xvm_r(0, 2, 1, 5, 7));
    test_xvm_image_word(image, 7, test_xvm_r(0x20, 2, 1, 5, 8));
    test_xvm_image_word(image, 8, test_xvm_r(0, 2, 1, 2, 9));
    test_xvm_image_word(image, 9, test_xvm_r(0, 2, 1, 3, 10));
    test_xvm_image_word(image, 10, test_xvm_i(0x0f, 1, 4, 11, 0x13));
    test_xvm_image_word(image, 11, test_xvm_i(0x0f, 1, 6, 12, 0x13));
    test_xvm_image_word(image, 12, test_xvm_i(0x0f, 1, 7, 13, 0x13));
    test_xvm_image_word(image, 13, test_xvm_i(1, 1, 1, 14, 0x13));
    test_xvm_image_word(image, 14, test_xvm_i(1, 1, 5, 15, 0x13));
    test_xvm_image_word(image, 15, test_xvm_i(0x401, 1, 5, 16, 0x13));
    test_xvm_image_word(image, 16, test_xvm_i(1, 1, 2, 17, 0x13));
    test_xvm_image_word(image, 17, test_xvm_i(1, 1, 3, 18, 0x13));
    test_xvm_image_word(image, 18, 0x0000000fU);
    test_xvm_image_word(image, 19, 0x00000073U);

    ret = test_xvm_prepare(&xvm, data, image, 80);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 20), MR_XVM_EXIT);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 3, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0x5aU);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 8, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);

    /* Exercise both outcomes of every conditional branch. */
    branch[0] = test_xvm_b(8, 2, 1, 0);
    branch[1] = test_xvm_b(8, 2, 1, 1);
    branch[2] = test_xvm_b(8, 2, 1, 4);
    branch[3] = test_xvm_b(8, 2, 1, 5);
    branch[4] = test_xvm_b(8, 2, 1, 6);
    branch[5] = test_xvm_b(8, 2, 1, 7);
    for (i = 0; i < 6; i++) {
        test_xvm_image_word(image, 0, branch[i]);
        test_xvm_image_word(image, 1, test_xvm_i(7, 0, 0, 3, 0x13));
        test_xvm_image_word(image, 2, test_xvm_i(9, 0, 0, 3, 0x13));
        ret = test_xvm_prepare(&xvm, data, image, 12);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
        ret = mr_xvm_reg_store(&xvm, 1, (i & 1U) ? 1 : 2);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
        ret = mr_xvm_reg_store(&xvm, 2, 2);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
        MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 2), MR_XVM_NONE);
        MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);
    }
}

MR_TEST(xvm, riscv32_csr_register_matrix) {
    mr_uint8_t image[80] = {0};
    mr_uint8_t data[TEST_XVM_DATA_SIZE];
    mr_xvm_event_t event = {0};
    mr_xvm_t xvm;
    mr_uint64_t value;
    mr_uint32_t i;
    mr_err_t ret;

    static const mr_uint32_t csr[] = {
        REG_CSR_MSTATUS,   REG_CSR_MISA,      REG_CSR_MIE,      REG_CSR_MTVEC,
        REG_CSR_MSCRATCH,  REG_CSR_MEPC,      REG_CSR_MCAUSE,   REG_CSR_MTVAL,
        REG_CSR_MIP,       REG_CSR_MCYCLE,    REG_CSR_MINSTRET, REG_CSR_MCYCLEH,
        REG_CSR_MINSTRETH, REG_CSR_MVENDORID, REG_CSR_MARCHID,  REG_CSR_MIMPID,
        REG_CSR_MHARTID,
    };

    for (i = 0; i < (sizeof(csr) / sizeof(csr[0])); i++) {
        test_xvm_image_word(image, i, test_xvm_i(csr[i], 0, 2, 1, 0x73));
    }
    test_xvm_image_word(image, 17, 0x00000073U);

    ret = test_xvm_prepare(&xvm, data, image, 72);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 18), MR_XVM_EXIT);

    /* Target register numbers are 0-32; CSR addresses are rejected */
    ret = mr_xvm_reg_load(&xvm, REG_CSR_MSTATUS, &value);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EINVAL);
    ret = mr_xvm_reg_store(&xvm, REG_CSR_MSTATUS, 1);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EINVAL);
    ret = mr_xvm_reg_load(&xvm, 0xFFFFFFFFU, &value);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EINVAL);
    ret = mr_xvm_reg_store(&xvm, 0xFFFFFFFFU, 0);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);
}

MR_TEST(xvm, riscv32_mmio_interrupt_registers) {
    mr_uint8_t image[24] = {0};
    mr_uint8_t data[TEST_XVM_DATA_SIZE];
    mr_xvm_event_t event = {0};
    mr_xvm_t xvm;
    mr_uint64_t value;
    mr_err_t ret;

    /* Read pending/enable registers and clear a pending interrupt through MMIO.
     */
    test_xvm_image_word(image, 0, test_xvm_u(CORE_MMIO_BASE, 1, 0x37));
    test_xvm_image_word(image, 1, test_xvm_i(1, 0, 0, 2, 0x13));
    test_xvm_image_word(image, 2, test_xvm_i(0, 1, 2, 3, 0x03));
    test_xvm_image_word(image, 3, test_xvm_i(4, 1, 2, 4, 0x03));
    test_xvm_image_word(image, 4, test_xvm_s(4, 2, 1, 2));
    test_xvm_image_word(image, 5, test_xvm_s(0, 2, 1, 2));
    test_xvm_image_word(image, 6, 0x00000073U);
    ret = test_xvm_prepare(&xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_xvm_interrupt(&xvm, 3, MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 7), MR_XVM_EXCEPT);
    ret = mr_xvm_reg_load(&xvm, 3, &value);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(value, 0U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);

    /* An address inside the MMIO window but outside the interrupt block fails.
     */
    test_xvm_image_word(image, 0, test_xvm_u(CORE_MMIO_BASE, 1, 0x37));
    test_xvm_image_word(image, 1, test_xvm_i(0x500, 1, 2, 2, 0x03));
    ret = test_xvm_prepare(&xvm, data, image, 8);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 2), MR_XVM_EXCEPT);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);

    /* An invalid MMIO store follows the same access-error path. */
    test_xvm_image_word(image, 0, test_xvm_u(CORE_MMIO_BASE, 1, 0x37));
    test_xvm_image_word(image, 1, test_xvm_i(1, 0, 0, 2, 0x13));
    test_xvm_image_word(image, 2, test_xvm_s(0x500, 2, 1, 2));
    ret = test_xvm_prepare(&xvm, data, image, 12);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 3), MR_XVM_EXCEPT);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);
}

#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */
#endif /* defined(MR_USE_TEST) */
