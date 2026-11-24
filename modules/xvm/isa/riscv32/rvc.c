/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/modules/xvm/isa/riscv32/rvc.h>
#if defined(MR_USE_MODULE_XVM_RISCV32_C)

/* Compressed register mapping (x8..x15) */
#define RVC_REG_PRIME(_r)               (8U + ((_r) & 0x7U))

/* 32-bit RV32I encoders used to expand RV32C instructions */
MR_INLINE mr_uint32_t rvc_enc_r(mr_uint32_t funct7, mr_uint32_t rs2,
                                mr_uint32_t rs1, mr_uint32_t funct3,
                                mr_uint32_t rd, mr_uint32_t opcode) {
    return ((funct7 & 0x7fU) << 25) | ((rs2 & 0x1fU) << 20)
         | ((rs1 & 0x1fU) << 15) | ((funct3 & 0x7U) << 12) | ((rd & 0x1fU) << 7)
         | (opcode & 0x7fU);
}

MR_INLINE mr_uint32_t rvc_enc_i(mr_uint32_t imm, mr_uint32_t rs1,
                                mr_uint32_t funct3, mr_uint32_t rd,
                                mr_uint32_t opcode) {
    return ((imm & 0xfffU) << 20) | ((rs1 & 0x1fU) << 15)
         | ((funct3 & 0x7U) << 12) | ((rd & 0x1fU) << 7) | (opcode & 0x7fU);
}

MR_INLINE mr_uint32_t rvc_enc_s(mr_uint32_t imm, mr_uint32_t rs2,
                                mr_uint32_t rs1, mr_uint32_t funct3,
                                mr_uint32_t opcode) {
    return (((imm >> 5) & 0x7fU) << 25) | ((rs2 & 0x1fU) << 20)
         | ((rs1 & 0x1fU) << 15) | ((funct3 & 0x7U) << 12)
         | ((imm & 0x1fU) << 7) | (opcode & 0x7fU);
}

MR_INLINE mr_uint32_t rvc_enc_b(mr_uint32_t imm, mr_uint32_t rs2,
                                mr_uint32_t rs1, mr_uint32_t funct3,
                                mr_uint32_t opcode) {
    return (((imm >> 12) & 0x1U) << 31) | (((imm >> 5) & 0x3fU) << 25)
         | ((rs2 & 0x1fU) << 20) | ((rs1 & 0x1fU) << 15)
         | ((funct3 & 0x7U) << 12) | (((imm >> 1) & 0xfU) << 8)
         | (((imm >> 11) & 0x1U) << 7) | (opcode & 0x7fU);
}

MR_INLINE mr_uint32_t rvc_enc_u(mr_uint32_t imm, mr_uint32_t rd,
                                mr_uint32_t opcode) {
    return (imm & 0xfffff000U) | ((rd & 0x1fU) << 7) | (opcode & 0x7fU);
}

MR_INLINE mr_uint32_t rvc_enc_j(mr_uint32_t imm, mr_uint32_t rd,
                                mr_uint32_t opcode) {
    return (((imm >> 20) & 0x1U) << 31) | (((imm >> 1) & 0x3ffU) << 21)
         | (((imm >> 11) & 0x1U) << 20) | (((imm >> 12) & 0xffU) << 12)
         | ((rd & 0x1fU) << 7) | (opcode & 0x7fU);
}

MR_INLINE mr_uint32_t rvc_sext(mr_uint32_t val, mr_uint32_t bits) {
    mr_uint32_t sign;

    sign = 1U << (bits - 1U);
    return (val ^ sign) - sign;
}

MR_INLINE mr_uint32_t rvc_cj_imm(mr_uint32_t c) {
    mr_uint32_t imm;

    /* offset[11|4|9:8|10|6|7|3:1|5] */
    imm = (((c >> 12) & 0x1U) << 11) | (((c >> 11) & 0x1U) << 4)
        | (((c >> 9) & 0x3U) << 8) | (((c >> 8) & 0x1U) << 10)
        | (((c >> 7) & 0x1U) << 6) | (((c >> 6) & 0x1U) << 7)
        | (((c >> 3) & 0x7U) << 1) | (((c >> 2) & 0x1U) << 5);
    return rvc_sext(imm, 12U);
}

MR_INLINE mr_uint32_t rvc_cb_imm(mr_uint32_t c) {
    mr_uint32_t imm;

    /* offset[8|4:3|7:6|2:1|5] */
    imm = (((c >> 12) & 0x1U) << 8) | (((c >> 10) & 0x3U) << 3)
        | (((c >> 5) & 0x3U) << 6) | (((c >> 3) & 0x3U) << 1)
        | (((c >> 2) & 0x1U) << 5);
    return rvc_sext(imm, 9U);
}

static int rvc_expand_q0(mr_uint32_t c, mr_uint32_t *out) {
    mr_uint32_t funct3, rd, rs1, rs2, imm;

    funct3 = (c >> 13) & 0x7U;
    switch (funct3) {
        /* C.ADDI4SPN -> ADDI rd', x2, nzuimm */
        case 0x0U: {
            imm = (((c >> 11) & 0x3U) << 4) | (((c >> 7) & 0xfU) << 6)
                | (((c >> 6) & 0x1U) << 2) | (((c >> 5) & 0x1U) << 3);
            if (imm == 0U) {
                return -MR_XVM_EXCEPT_NR_ILLEGAL;
            }
            rd = RVC_REG_PRIME(c >> 2);
            *out = rvc_enc_i(imm, 2U, 0x0U, rd, 0b0010011U);
            return 0;
        }
        /* C.LW -> LW rd', uimm(rs1') */
        case 0x2U: {
            imm = (((c >> 10) & 0x7U) << 3) | (((c >> 6) & 0x1U) << 2)
                | (((c >> 5) & 0x1U) << 6);
            rs1 = RVC_REG_PRIME(c >> 7);
            rd = RVC_REG_PRIME(c >> 2);
            *out = rvc_enc_i(imm, rs1, 0x2U, rd, 0b0000011U);
            return 0;
        }
        /* C.SW -> SW rs2', uimm(rs1') */
        case 0x6U: {
            imm = (((c >> 10) & 0x7U) << 3) | (((c >> 6) & 0x1U) << 2)
                | (((c >> 5) & 0x1U) << 6);
            rs1 = RVC_REG_PRIME(c >> 7);
            rs2 = RVC_REG_PRIME(c >> 2);
            *out = rvc_enc_s(imm, rs2, rs1, 0x2U, 0b0100011U);
            return 0;
        }
        default: {
            return -MR_XVM_EXCEPT_NR_ILLEGAL;
        }
    }
}

static int rvc_expand_q1(mr_uint32_t c, mr_uint32_t *out) {
    mr_uint32_t funct3, funct2, sub_op, rd, rs1, rs2, imm, shamt;

    funct3 = (c >> 13) & 0x7U;
    switch (funct3) {
        /* C.NOP / C.ADDI -> ADDI rd, rd, nzimm */
        case 0x0U: {
            rd = (c >> 7) & 0x1fU;
            imm = rvc_sext((((c >> 12) & 0x1U) << 5) | ((c >> 2) & 0x1fU), 6U);
            *out = rvc_enc_i(imm, rd, 0x0U, rd, 0b0010011U);
            return 0;
        }
        /* C.JAL -> JAL x1, offset */
        case 0x1U: {
            imm = rvc_cj_imm(c);
            *out = rvc_enc_j(imm, 1U, 0b1101111U);
            return 0;
        }
        /* C.LI -> ADDI rd, x0, imm */
        case 0x2U: {
            rd = (c >> 7) & 0x1fU;
            imm = rvc_sext((((c >> 12) & 0x1U) << 5) | ((c >> 2) & 0x1fU), 6U);
            *out = rvc_enc_i(imm, 0U, 0x0U, rd, 0b0010011U);
            return 0;
        }
        /* C.ADDI16SP / C.LUI */
        case 0x3U: {
            rd = (c >> 7) & 0x1fU;
            if (rd == 2U) {
                /* C.ADDI16SP -> ADDI x2, x2, nzimm */
                imm = (((c >> 12) & 0x1U) << 9) | (((c >> 6) & 0x1U) << 4)
                    | (((c >> 5) & 0x1U) << 6) | (((c >> 3) & 0x3U) << 7)
                    | (((c >> 2) & 0x1U) << 5);
                if (imm == 0U) {
                    return -MR_XVM_EXCEPT_NR_ILLEGAL;
                }
                imm = rvc_sext(imm, 10U);
                *out = rvc_enc_i(imm, 2U, 0x0U, 2U, 0b0010011U);
            } else {
                /* C.LUI -> LUI rd, nzimm */
                imm = (((c >> 12) & 0x1U) << 17) | (((c >> 2) & 0x1fU) << 12);
                if (imm == 0U) {
                    return -MR_XVM_EXCEPT_NR_ILLEGAL;
                }
                imm = rvc_sext(imm, 18U);
                *out = rvc_enc_u(imm, rd, 0b0110111U);
            }
            return 0;
        }
        /* C.SRLI / C.SRAI / C.ANDI / C.SUB / C.XOR / C.OR / C.AND */
        case 0x4U: {
            funct2 = (c >> 10) & 0x3U;
            rd = RVC_REG_PRIME(c >> 7);
            if (funct2 == 0x0U) {
                /* C.SRLI -> SRLI rd', rd', shamt */
                if (((c >> 12) & 0x1U) != 0U) {
                    return -MR_XVM_EXCEPT_NR_ILLEGAL;
                }
                shamt = (c >> 2) & 0x1fU;
                *out = rvc_enc_i(shamt, rd, 0x5U, rd, 0b0010011U);
                return 0;
            }
            if (funct2 == 0x1U) {
                /* C.SRAI -> SRAI rd', rd', shamt */
                if (((c >> 12) & 0x1U) != 0U) {
                    return -MR_XVM_EXCEPT_NR_ILLEGAL;
                }
                shamt = (c >> 2) & 0x1fU;
                *out = rvc_enc_i(0x400U | shamt, rd, 0x5U, rd, 0b0010011U);
                return 0;
            }
            if (funct2 == 0x2U) {
                /* C.ANDI -> ANDI rd', rd', imm */
                imm = rvc_sext((((c >> 12) & 0x1U) << 5) | ((c >> 2) & 0x1fU),
                               6U);
                *out = rvc_enc_i(imm, rd, 0x7U, rd, 0b0010011U);
                return 0;
            }
            /* Register-register compressed ALU */
            if (((c >> 12) & 0x1U) != 0U) {
                return -MR_XVM_EXCEPT_NR_ILLEGAL;
            }
            sub_op = (c >> 5) & 0x3U;
            rs2 = RVC_REG_PRIME(c >> 2);
            switch (sub_op) {
                /* C.SUB -> SUB rd', rd', rs2' */
                case 0x0U: {
                    *out = rvc_enc_r(0x20U, rs2, rd, 0x0U, rd, 0b0110011U);
                    return 0;
                }
                /* C.XOR -> XOR rd', rd', rs2' */
                case 0x1U: {
                    *out = rvc_enc_r(0x00U, rs2, rd, 0x4U, rd, 0b0110011U);
                    return 0;
                }
                /* C.OR -> OR rd', rd', rs2' */
                case 0x2U: {
                    *out = rvc_enc_r(0x00U, rs2, rd, 0x6U, rd, 0b0110011U);
                    return 0;
                }
                /* C.AND -> AND rd', rd', rs2' */
                case 0x3U: {
                    *out = rvc_enc_r(0x00U, rs2, rd, 0x7U, rd, 0b0110011U);
                    return 0;
                }
                default: {
                    return -MR_XVM_EXCEPT_NR_ILLEGAL;
                }
            }
        }
        /* C.J -> JAL x0, offset */
        case 0x5U: {
            imm = rvc_cj_imm(c);
            *out = rvc_enc_j(imm, 0U, 0b1101111U);
            return 0;
        }
        /* C.BEQZ -> BEQ rs1', x0, offset */
        case 0x6U: {
            rs1 = RVC_REG_PRIME(c >> 7);
            imm = rvc_cb_imm(c);
            *out = rvc_enc_b(imm, 0U, rs1, 0x0U, 0b1100011U);
            return 0;
        }
        /* C.BNEZ -> BNE rs1', x0, offset */
        case 0x7U: {
            rs1 = RVC_REG_PRIME(c >> 7);
            imm = rvc_cb_imm(c);
            *out = rvc_enc_b(imm, 0U, rs1, 0x1U, 0b1100011U);
            return 0;
        }
        default: {
            return -MR_XVM_EXCEPT_NR_ILLEGAL;
        }
    }
}

static int rvc_expand_q2(mr_uint32_t c, mr_uint32_t *out) {
    mr_uint32_t funct3, bit12, rd, rs2, imm, shamt;

    funct3 = (c >> 13) & 0x7U;
    switch (funct3) {
        /* C.SLLI -> SLLI rd, rd, shamt */
        case 0x0U: {
            if (((c >> 12) & 0x1U) != 0U) {
                return -MR_XVM_EXCEPT_NR_ILLEGAL;
            }
            rd = (c >> 7) & 0x1fU;
            shamt = (c >> 2) & 0x1fU;
            *out = rvc_enc_i(shamt, rd, 0x1U, rd, 0b0010011U);
            return 0;
        }
        /* C.LWSP -> LW rd, uimm(x2) */
        case 0x2U: {
            rd = (c >> 7) & 0x1fU;
            if (rd == 0U) {
                return -MR_XVM_EXCEPT_NR_ILLEGAL;
            }
            imm = (((c >> 12) & 0x1U) << 5) | (((c >> 4) & 0x7U) << 2)
                | (((c >> 2) & 0x3U) << 6);
            *out = rvc_enc_i(imm, 2U, 0x2U, rd, 0b0000011U);
            return 0;
        }
        /* C.JR / C.MV / C.EBREAK / C.JALR / C.ADD */
        case 0x4U: {
            bit12 = (c >> 12) & 0x1U;
            rd = (c >> 7) & 0x1fU;
            rs2 = (c >> 2) & 0x1fU;
            if (bit12 == 0U) {
                if (rs2 == 0U) {
                    /* C.JR -> JALR x0, 0(rs1) */
                    if (rd == 0U) {
                        return -MR_XVM_EXCEPT_NR_ILLEGAL;
                    }
                    *out = rvc_enc_i(0U, rd, 0x0U, 0U, 0b1100111U);
                } else {
                    /* C.MV -> ADD rd, x0, rs2 */
                    *out = rvc_enc_r(0x00U, rs2, 0U, 0x0U, rd, 0b0110011U);
                }
                return 0;
            }
            if ((rd == 0U) && (rs2 == 0U)) {
                /* C.EBREAK -> EBREAK */
                *out = 0x00100073U;
                return 0;
            }
            if (rs2 == 0U) {
                /* C.JALR -> JALR x1, 0(rs1) */
                *out = rvc_enc_i(0U, rd, 0x0U, 1U, 0b1100111U);
                return 0;
            }
            /* C.ADD -> ADD rd, rd, rs2 */
            *out = rvc_enc_r(0x00U, rs2, rd, 0x0U, rd, 0b0110011U);
            return 0;
        }
        /* C.SWSP -> SW rs2, uimm(x2) */
        case 0x6U: {
            rs2 = (c >> 2) & 0x1fU;
            imm = (((c >> 9) & 0xfU) << 2) | (((c >> 7) & 0x3U) << 6);
            *out = rvc_enc_s(imm, rs2, 2U, 0x2U, 0b0100011U);
            return 0;
        }
        default: {
            return -MR_XVM_EXCEPT_NR_ILLEGAL;
        }
    }
}

int xvm_riscv32_rvc_expand(mr_uint16_t half, mr_uint32_t *out) {
    mr_uint32_t c;

    c = (mr_uint32_t)half;
    switch (c & 0x3U) {
        /* Quadrant 0 */
        case 0x0U: {
            return rvc_expand_q0(c, out);
        }
        /* Quadrant 1 */
        case 0x1U: {
            return rvc_expand_q1(c, out);
        }
        /* Quadrant 2 */
        case 0x2U: {
            return rvc_expand_q2(c, out);
        }
        default: {
            return -MR_XVM_EXCEPT_NR_ILLEGAL;
        }
    }
}
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_C) */
