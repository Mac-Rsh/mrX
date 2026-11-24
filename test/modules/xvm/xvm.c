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
    mr_xvm_execute(&xvm, &event, 0);
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

    /* An enabled external interrupt enters the machine trap vector via PLIC. */
    MR_TEST_ASSERT_EQ_INT(test_xvm_prepare(&xvm, data, image, sizeof(image)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_interrupt(&xvm, 0, MR_TRUE), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);

    test_xvm_image_word(image, 0, test_xvm_i(0x21, 0, 0, 6, 0x13));
    test_xvm_image_word(image, 1, test_xvm_i(REG_CSR_MTVEC, 6, 1, 0, 0x73));
    test_xvm_image_word(image, 2, test_xvm_i(REG_MSTATUS_MIE, 0, 0, 6, 0x13));
    test_xvm_image_word(image, 3, test_xvm_i(REG_CSR_MSTATUS, 6, 1, 0, 0x73));
    test_xvm_image_word(image, 4, test_xvm_i(REG_MIE_MEIE, 0, 0, 6, 0x13));
    test_xvm_image_word(image, 5, test_xvm_i(REG_CSR_MIE, 6, 1, 0, 0x73));
    /* Configure PLIC PRIORITY[11] = 1 (base + 44) and ENABLE = (1 << 11). */
    test_xvm_image_word(image, 6, test_xvm_u(CORE_MMIO_PLIC_BASE, 1, 0x37));
    test_xvm_image_word(image, 7, test_xvm_i(1, 0, 0, 2, 0x13));
    test_xvm_image_word(image, 8, test_xvm_s(11 * 4, 2, 1, 2));
    test_xvm_image_word(image, 9, test_xvm_u(CORE_MMIO_PLIC_ENABLE, 1, 0x37));
    test_xvm_image_word(image, 10, test_xvm_i(REG_MIE_MEIE, 0, 0, 2, 0x13));
    test_xvm_image_word(image, 11, test_xvm_s(0, 2, 1, 2));
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
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 12), MR_XVM_NONE);
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
    mr_uint8_t image[96] = {0};
    mr_uint8_t data[TEST_XVM_DATA_SIZE];
    mr_xvm_event_t event = {0};
    mr_xvm_t xvm;
    mr_uint64_t value;
    mr_err_t ret;

    /* Exercise PLIC PRIORITY, RO PENDING, ENABLE, THRESHOLD, CLAIM & COMPLETE.
     * IRQ 3 (prio 2) and IRQ 5 (prio 5) are pending; CLAIM returns 5 then 3,
     * and COMPLETE re-latches IRQ 5 while its level input remains asserted.
     */
    /* x1 = PLIC_BASE, x2 = PLIC_PENDING, x3 = PLIC_ENABLE, x4 = PLIC_CLAIM */
    test_xvm_image_word(image, 0, test_xvm_u(CORE_MMIO_PLIC_BASE, 1, 0x37));
    test_xvm_image_word(image, 1, test_xvm_i(2, 0, 0, 5, 0x13));
    test_xvm_image_word(image, 2, test_xvm_s(3 * 4, 5, 1, 2));
    test_xvm_image_word(image, 3, test_xvm_i(5, 0, 0, 5, 0x13));
    test_xvm_image_word(image, 4, test_xvm_s(5 * 4, 5, 1, 2));
    test_xvm_image_word(image, 5, test_xvm_u(CORE_MMIO_PLIC_PENDING, 2, 0x37));
    test_xvm_image_word(image, 6, test_xvm_s(0, 0, 2, 2)); /* RO: ignored */
    test_xvm_image_word(image, 7, test_xvm_i(0, 2, 2, 6, 0x03)); /* x6 = (1<<3)|(1<<5) */
    test_xvm_image_word(image, 8, test_xvm_u(CORE_MMIO_PLIC_ENABLE, 3, 0x37));
    test_xvm_image_word(image, 9, test_xvm_i((1 << 3) | (1 << 5), 0, 0, 5, 0x13));
    test_xvm_image_word(image, 10, test_xvm_s(0, 5, 3, 2));
    test_xvm_image_word(image, 11, test_xvm_u(CORE_MMIO_PLIC_THRESHOLD, 4, 0x37));
    test_xvm_image_word(image, 12, test_xvm_i(1, 0, 0, 5, 0x13));
    test_xvm_image_word(image, 13, test_xvm_s(0, 5, 4, 2)); /* threshold = 1 */
    test_xvm_image_word(image, 14, test_xvm_i(4, 4, 2, 7, 0x03)); /* claim -> x7 (5) */
    test_xvm_image_word(image, 15, test_xvm_i(4, 4, 2, 8, 0x03)); /* claim -> x8 (3) */
    test_xvm_image_word(image, 16, test_xvm_i(4, 4, 2, 9, 0x03)); /* claim -> x9 (0) */
    test_xvm_image_word(image, 17, test_xvm_s(4, 7, 4, 2)); /* complete(5) */
    test_xvm_image_word(image, 18, test_xvm_i(0, 2, 2, 10, 0x03)); /* x10 = pending */
    test_xvm_image_word(image, 19, 0x00000073U); /* ECALL */

    ret = test_xvm_prepare(&xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_interrupt(&xvm, 3, MR_TRUE), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_interrupt(&xvm, 5, MR_TRUE), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 20), MR_XVM_EXIT);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 6, &value), 0);
    MR_TEST_EXPECT_EQ_UINT(value, (1U << 3) | (1U << 5));
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 7, &value), 0);
    MR_TEST_EXPECT_EQ_UINT(value, 5U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 8, &value), 0);
    MR_TEST_EXPECT_EQ_UINT(value, 3U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 9, &value), 0);
    MR_TEST_EXPECT_EQ_UINT(value, 0U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 10, &value), 0);
    MR_TEST_EXPECT_EQ_UINT(value, (1U << 5));
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);

    /* Exercise CLINT MTIMECMP / MTIME triggering MTIP (mcause = 0x80000007). */
    test_xvm_image_word(image, 0, test_xvm_i(0x24, 0, 0, 6, 0x13));
    test_xvm_image_word(image, 1, test_xvm_i(REG_CSR_MTVEC, 6, 1, 0, 0x73));
    test_xvm_image_word(image, 2, test_xvm_i(REG_MSTATUS_MIE, 0, 0, 6, 0x13));
    test_xvm_image_word(image, 3, test_xvm_i(REG_CSR_MSTATUS, 6, 1, 0, 0x73));
    test_xvm_image_word(image, 4, test_xvm_i(REG_MIE_MTIE, 0, 0, 6, 0x13));
    test_xvm_image_word(image, 5, test_xvm_i(REG_CSR_MIE, 6, 1, 0, 0x73));
    test_xvm_image_word(image, 6, test_xvm_u(CORE_MMIO_CLINT_BASE + 0x4000U, 1, 0x37));
    test_xvm_image_word(image, 7, test_xvm_s(4, 0, 1, 2)); /* mtimecmp_hi = 0 */
    test_xvm_image_word(image, 8, test_xvm_s(0, 0, 1, 2)); /* mtimecmp_lo = 0 */
    test_xvm_image_word(image, 9, test_xvm_i(REG_CSR_MCAUSE, 0, 2, 5, 0x73));
    ret = test_xvm_prepare(&xvm, data, image, 40);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 11), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 5, &value), 0);
    MR_TEST_EXPECT_EQ_UINT(value, REG_MCAUSE_MTI_INTR);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);

    /* An address inside the MMIO window but outside PLIC/CLINT fails. */
    test_xvm_image_word(image, 0, test_xvm_u(CORE_MMIO_PLIC_END, 1, 0x37));
    test_xvm_image_word(image, 1, test_xvm_i(0x500, 1, 2, 2, 0x03));
    ret = test_xvm_prepare(&xvm, data, image, 8);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 2), MR_XVM_EXCEPT);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);

    /* An invalid MMIO store follows the same access-error path. */
    test_xvm_image_word(image, 0, test_xvm_u(CORE_MMIO_PLIC_END, 1, 0x37));
    test_xvm_image_word(image, 1, test_xvm_i(1, 0, 0, 2, 0x13));
    test_xvm_image_word(image, 2, test_xvm_s(0x500, 2, 1, 2));
    ret = test_xvm_prepare(&xvm, data, image, 12);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 3), MR_XVM_EXCEPT);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);
}

MR_TEST(xvm, riscv32_alignment_init_guards_and_range) {
    mr_uint8_t raw_buf[TEST_XVM_DATA_SIZE + 16U];
    mr_uint8_t image[12] = {0};
    mr_uint8_t tmp[4] = {0x11, 0x22, 0x33, 0x44};
    mr_uint8_t out[4] = {0};
    mr_xvm_event_t event = {0};
    mr_bool_t writable = MR_FALSE;
    void *mapped = MR_NULL;
    mr_xvm_t xvm;
    mr_uint64_t value = 0;
    mr_err_t ret;

    /* Access before load must be rejected with -MR_EPERM. */
    ret = mr_xvm_init(&xvm, "riscv32", raw_buf + 3U, TEST_XVM_DATA_SIZE);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 1, &value), -MR_EPERM);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_store(&xvm, 1, 5U), -MR_EPERM);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_datamem_load(&xvm, 0, out, sizeof(out)),
                          -MR_EPERM);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_datamem_store(&xvm, 0, tmp, sizeof(tmp)),
                          -MR_EPERM);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_datamem_map(&xvm, 0, &mapped, &writable),
                          -MR_EPERM);

    /* RAM overlapping MMIO or wrapping 32-bit address space is rejected. */
    test_xvm_image_word(image, 0, test_xvm_i(7, 0, 0, 1, 0x13));
    test_xvm_image_word(image, 1, 0x00000073U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_load(&xvm, image, 8, CORE_MMIO_BASE),
                          -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_status(&xvm), MR_XVM_STATUS_INIT);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_load(&xvm, image, 8, 0xFFFFFFF0U), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_load(&xvm, image, 8, 0x100000000ULL),
                          -MR_EINVAL);

    /* Unaligned backing buffer works cleanly once loaded at a valid entry. */
    MR_TEST_ASSERT_EQ_INT(mr_xvm_load(&xvm, image, 8, TEST_XVM_ENTRY), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xvm_datamem_store(&xvm, TEST_XVM_ENTRY, tmp, sizeof(tmp)),
        (mr_ssize_t)sizeof(tmp));
    MR_TEST_ASSERT_EQ_INT(
        mr_xvm_datamem_load(&xvm, TEST_XVM_ENTRY, out, sizeof(out)),
        (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_INT(mr_memcmp(tmp, out, sizeof(tmp)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 2), MR_XVM_EXIT);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 1, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 7U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);
}

MR_TEST(xvm, riscv32_strict_decode_ro_csr_and_signed_ops) {
    mr_uint8_t data[TEST_XVM_DATA_SIZE];
    mr_uint8_t image[32] = {0};
    mr_xvm_event_t event = {0};
    mr_xvm_t xvm;
    mr_uint64_t value = 0;
    mr_err_t ret;

    /* R-type with illegal funct7 (0x20 on SLL) must trap as illegal. */
    test_xvm_image_word(image, 0, test_xvm_r(0x20, 2, 1, 1, 3));
    ret = test_xvm_prepare(&xvm, data, image, 4);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 1), MR_XVM_EXCEPT);
    MR_TEST_ASSERT_EQ_UINT(event.except.nr, MR_XVM_EXCEPT_NR_ILLEGAL);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);

    /* ECALL with non-zero rd must trap as illegal. */
    test_xvm_image_word(image, 0, test_xvm_i(0x000, 0, 0, 1, 0x73));
    ret = test_xvm_prepare(&xvm, data, image, 4);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 1), MR_XVM_EXCEPT);
    MR_TEST_ASSERT_EQ_UINT(event.except.nr, MR_XVM_EXCEPT_NR_ILLEGAL);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);

    /* Writing read-only CSR (CYCLE / MHARTID) must trap as illegal. */
    test_xvm_image_word(image, 0, test_xvm_i(REG_CSR_CYCLE, 1, 1, 0, 0x73));
    ret = test_xvm_prepare(&xvm, data, image, 4);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 1), MR_XVM_EXCEPT);
    MR_TEST_ASSERT_EQ_UINT(event.except.nr, MR_XVM_EXCEPT_NR_ILLEGAL);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);

    test_xvm_image_word(image, 0, test_xvm_i(REG_CSR_MHARTID, 1, 1, 0, 0x73));
    ret = test_xvm_prepare(&xvm, data, image, 4);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 1), MR_XVM_EXCEPT);
    MR_TEST_ASSERT_EQ_UINT(event.except.nr, MR_XVM_EXCEPT_NR_ILLEGAL);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);

    /* Negative SRA/SRAI and signed MULH/MULHSU produce exact 32-bit results. */
    /* x1 = 0x80000000; x2 = 4; x3 = x1 >>a x2 (0xF8000000);
     * x4 = x1 >>a 4 (0xF8000000); x5 = MULH(-2, 3) (-1);
     * x6 = MULHSU(-1, 2) (-1) */
    test_xvm_image_word(image, 0, test_xvm_u(0x80000000U, 1, 0x37));
    test_xvm_image_word(image, 1, test_xvm_i(4, 0, 0, 2, 0x13));
    test_xvm_image_word(image, 2, test_xvm_r(0x20, 2, 1, 5, 3));
    test_xvm_image_word(image, 3, test_xvm_i(0x404, 1, 5, 4, 0x13));
    test_xvm_image_word(image, 4, test_xvm_i(0xffe, 0, 0, 7, 0x13));
    test_xvm_image_word(image, 5, test_xvm_i(3, 0, 0, 8, 0x13));
    test_xvm_image_word(image, 6, test_xvm_r(0x01, 8, 7, 1, 5));
    test_xvm_image_word(image, 7, test_xvm_r(0x01, 2, 7, 2, 6));
    ret = test_xvm_prepare(&xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 8), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 3, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0xf8000000U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 4, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0xf8000000U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 5, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0xffffffffU);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 6, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0xffffffffU);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);
}

#if defined(MR_USE_MODULE_XVM_MMIO)
typedef struct test_mmio_dev {
    mr_uint8_t regs[16];
    mr_size_t last_load_size;
    mr_size_t last_store_size;
    mr_uint32_t load_cnt;
    mr_uint32_t store_cnt;
} test_mmio_dev_t;

static mr_err_t test_mmio_load_cb(void *user, mr_uint64_t offset,
                                  mr_size_t size, mr_uint64_t *val) {
    test_mmio_dev_t *dev;
    mr_uint32_t tmp;

    dev = (test_mmio_dev_t *)user;
    tmp = 0U;
    mr_memcpy(&tmp, dev->regs + offset, size);
    dev->last_load_size = size;
    dev->load_cnt++;
    *val = tmp;
    return 0;
}

static mr_err_t test_mmio_store_cb(void *user, mr_uint64_t offset,
                                   mr_size_t size, mr_uint64_t val) {
    test_mmio_dev_t *dev;
    mr_uint32_t tmp;

    dev = (test_mmio_dev_t *)user;
    tmp = (mr_uint32_t)val;
    mr_memcpy(dev->regs + offset, &tmp, size);
    dev->last_store_size = size;
    dev->store_cnt++;
    return 0;
}

static const mr_xvm_mmio_ops_t test_mmio_ops = {
    .load = test_mmio_load_cb,
    .store = test_mmio_store_cb,
};

MR_TEST(xvm, mmio_register_and_access) {
    mr_uint8_t data[TEST_XVM_DATA_SIZE];
    mr_uint8_t image[48] = {0};
    mr_xvm_event_t event = {0};
    test_mmio_dev_t dev;
    mr_xvm_t xvm;
    mr_uint64_t value;
    mr_err_t ret;
    mr_uint32_t base;

    mr_memset(&dev, 0, sizeof(dev));
    base = CORE_MMIO_PLIC_END + 0x1000U;

    /* SW x2, 0(x1); SH x3, 4(x1); SB x4, 6(x1);
     * LW x5, 0(x1); LH x6, 4(x1); LHU x7, 4(x1);
     * LB x8, 6(x1); LBU x9, 6(x1); LW x10, 0x20(x1) (unmapped -> ACCESS) */
    test_xvm_image_word(image, 0, test_xvm_s(0, 2, 1, 2));
    test_xvm_image_word(image, 1, test_xvm_s(4, 3, 1, 1));
    test_xvm_image_word(image, 2, test_xvm_s(6, 4, 1, 0));
    test_xvm_image_word(image, 3, test_xvm_i(0, 1, 2, 5, 0x03));
    test_xvm_image_word(image, 4, test_xvm_i(4, 1, 1, 6, 0x03));
    test_xvm_image_word(image, 5, test_xvm_i(4, 1, 5, 7, 0x03));
    test_xvm_image_word(image, 6, test_xvm_i(6, 1, 0, 8, 0x03));
    test_xvm_image_word(image, 7, test_xvm_i(6, 1, 4, 9, 0x03));
    test_xvm_image_word(image, 8, test_xvm_i(0x20, 1, 2, 10, 0x03));

    ret = test_xvm_prepare(&xvm, data, image, 36);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    /* Registration validation: zero size, overlap, unregister, re-register. */
    MR_TEST_ASSERT_EQ_INT(
        mr_xvm_mmio_register(&xvm, base, 0U, &test_mmio_ops, &dev), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(
        mr_xvm_mmio_register(&xvm, base, sizeof(dev.regs), &test_mmio_ops,
                             &dev),
        0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xvm_mmio_register(&xvm, base + 4U, 8U, &test_mmio_ops, &dev),
        -MR_EEXIST);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_mmio_unregister(&xvm, base + 4U), -MR_ENOENT);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_mmio_unregister(&xvm, base), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xvm_mmio_register(&xvm, base, sizeof(dev.regs), &test_mmio_ops,
                             &dev),
        0);

    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_store(&xvm, 1, base), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_store(&xvm, 2, 0x11223344U), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_store(&xvm, 3, 0x8001U), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_store(&xvm, 4, 0xfeU), 0);

    /* Execute 8 valid MMIO operations (1/2/4-byte store and load). */
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 8), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_UINT(dev.store_cnt, 3U);
    MR_TEST_ASSERT_EQ_UINT(dev.load_cnt, 5U);
    MR_TEST_ASSERT_EQ_UINT(dev.last_store_size, 1U);
    MR_TEST_ASSERT_EQ_UINT(dev.last_load_size, 1U);

    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 5, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0x11223344U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 6, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0xffff8001U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 7, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0x00008001U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 8, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0xfffffffeU);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 9, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0x000000feU);

    /* Accessing unmapped MMIO offset raises ACCESS exception. */
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 1), MR_XVM_EXCEPT);
    MR_TEST_ASSERT_EQ_UINT(event.except.nr, MR_XVM_EXCEPT_NR_ACCESS);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);
}
#endif /* defined(MR_USE_MODULE_XVM_MMIO) */

#if defined(MR_USE_MODULE_XVM_RISCV32_C)
static void test_xvm_image_half(mr_uint8_t *image, mr_size_t byte_off,
                                mr_uint16_t value) {
    mr_memcpy(image + byte_off, &value, sizeof(value));
}

MR_TEST(xvm, riscv32_c_extension_and_illegal) {
    mr_uint8_t data[TEST_XVM_DATA_SIZE];
    mr_uint8_t image[64] = {0};
    mr_xvm_event_t event = {0};
    mr_xvm_t xvm;
    mr_uint64_t value;
    mr_err_t ret;
    mr_size_t off;
    mr_uint32_t w;

    /* Mixed 16-bit RV32C and 2-byte-aligned 32-bit RV32I program:
     * 0x00: C.LI x8, 10            (0x4429) -> x8 = 10
     * 0x02: CSRRS x9, misa, x0     (32-bit at 2-byte aligned PC!) -> x9 = misa
     * 0x06: C.ADDI x8, 5           (0x0415) -> x8 = 15
     * 0x08: C.SLLI x8, 1           (0x0406) -> x8 = 30
     * 0x0a: C.SRLI x8, 1           (0x8005) -> x8 = 15
     * 0x0c: C.LI x9, 6             (0x4499) -> x9 = 6
     * 0x0e: C.SUB x8, x9           (0x8c05) -> x8 = 9
     * 0x10: C.XOR x8, x9           (0x8c25) -> x8 = 15
     * 0x12: C.OR  x8, x9           (0x8c45) -> x8 = 15
     * 0x14: C.AND x8, x9           (0x8c65) -> x8 = 6
     * 0x16: C.ADDI16SP x2, -16     (0x717d) -> sp -= 16
     * 0x18: C.SWSP x8, 0(x2)       (0xc022) -> [sp] = 6
     * 0x1a: C.LWSP x10, 0(x2)      (0x4502) -> x10 = 6
     * 0x1c: C.MV x11, x10          (0x85aa) -> x11 = 6
     * 0x1e: C.ADD x11, x8          (0x95a2) -> x11 = 12
     * 0x20: C.JAL +4 (to 0x24)     (0x2011) -> x1 = 0x22, pc = 0x24
     * 0x22: C.LI x11, 0            (0x4581) -> skipped
     * 0x24: C.BNEZ x11, +4 (0x28)  (0xe191) -> branch to 0x28
     * 0x26: C.LI x11, 0            (0x4581) -> skipped
     * 0x28: C.EBREAK               (0x9002) -> BREAK */
    off = 0U;
    test_xvm_image_half(image, off, 0x4429U);
    off += 2U;
    w = test_xvm_i(REG_CSR_MISA, 0, 2, 9, 0x73);
    mr_memcpy(image + off, &w, sizeof(w));
    off += 4U;
    test_xvm_image_half(image, off, 0x0415U);
    off += 2U;
    test_xvm_image_half(image, off, 0x0406U);
    off += 2U;
    test_xvm_image_half(image, off, 0x8005U);
    off += 2U;
    test_xvm_image_half(image, off, 0x4499U);
    off += 2U;
    test_xvm_image_half(image, off, 0x8c05U);
    off += 2U;
    test_xvm_image_half(image, off, 0x8c25U);
    off += 2U;
    test_xvm_image_half(image, off, 0x8c45U);
    off += 2U;
    test_xvm_image_half(image, off, 0x8c65U);
    off += 2U;
    test_xvm_image_half(image, off, 0x717dU);
    off += 2U;
    test_xvm_image_half(image, off, 0xc022U);
    off += 2U;
    test_xvm_image_half(image, off, 0x4502U);
    off += 2U;
    test_xvm_image_half(image, off, 0x85aaU);
    off += 2U;
    test_xvm_image_half(image, off, 0x95a2U);
    off += 2U;
    test_xvm_image_half(image, off, 0x2011U);
    off += 2U;
    test_xvm_image_half(image, off, 0x4581U);
    off += 2U;
    test_xvm_image_half(image, off, 0xe191U);
    off += 2U;
    test_xvm_image_half(image, off, 0x4581U);
    off += 2U;
    test_xvm_image_half(image, off, 0x9002U);
    off += 2U;

    ret = test_xvm_prepare(&xvm, data, image, off);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_store(&xvm, 2, TEST_XVM_ENTRY + 0x40U), 0);

    /* First 2 instructions: check MISA reports C extension bit. */
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 2), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 9, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, CORE_MISA_DEFAULT);

    /* Run until C.EBREAK. */
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 32), MR_XVM_EXCEPT);
    MR_TEST_ASSERT_EQ_UINT(event.except.nr, MR_XVM_EXCEPT_NR_BREAK);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 1, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0x22U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 10, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 6U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 11, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 12U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 32, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0x28U);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);

    /* Illegal RV32C encodings: 0x0000, C.ADDI16SP(0), C.LUI(0), C.LWSP(rd=0),
     * C.JR(rs1=0), C.SLLI(shamt[5]=1). */
    {
        static const mr_uint16_t illegal_rvc[] = {
            0x0000U, 0x6101U, 0x6181U, 0x4002U, 0x8002U, 0x1086U,
        };
        mr_size_t idx;

        for (idx = 0U; idx < (sizeof(illegal_rvc) / sizeof(illegal_rvc[0]));
             idx++) {
            test_xvm_image_half(image, 0U, illegal_rvc[idx]);
            ret = test_xvm_prepare(&xvm, data, image, 2U);
            MR_TEST_ASSERT_EQ_INT(ret, 0);
            MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 1),
                                  MR_XVM_EXCEPT);
            MR_TEST_ASSERT_EQ_UINT(event.except.nr, MR_XVM_EXCEPT_NR_ILLEGAL);
            MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);
        }
    }
}
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_C) */

#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */
#endif /* defined(MR_USE_TEST) */
