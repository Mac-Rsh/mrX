/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/modules/xvm/mr_xvm_gdb.h>
#if defined(MR_USE_MODULE_XVM_GDB)
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_sprintf.h>
#include <mrX/libc/mr_string.h>

/* XVM GDB clazzes(static & dynamic) */
static void gdb_statik_release(mr_object_t *obj);
static void gdb_dynamic_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(xvm_gdb, gdb_statik_release, gdb_dynamic_release, MR_NULL);

/* XVM GDB hex conversion auxiliary array */
static const char gdb_hex[] = "0123456789abcdef";

/* Command result definition */
typedef enum gdb_cmd {
    GDB_CMD_REPLY,
    GDB_CMD_SILENT,
    GDB_CMD_CLOSE,
    GDB_CMD_KILL,
} gdb_cmd_t;

/* XML window type */
typedef struct gdb_xml_win {
    char *buf;
    mr_size_t cap;
    mr_size_t offset;
    mr_size_t want;
    mr_size_t pos;
    mr_size_t len;
} gdb_xml_win_t;

/* XVM GDB encode definition */
#define GDB_REPLY_OVERHEAD              (4)
#define GDB_REG_BYTES_MAX               (8)
#define GDB_PC_NONE                     (MR_UINT32_MAX)

/* XVM GDB signal definition */
#define GDB_SIGNAL_INT                  (2)
#define GDB_SIGNAL_ILL                  (4)
#define GDB_SIGNAL_TRAP                 (5)
#define GDB_SIGNAL_ABRT                 (6)
#define GDB_SIGNAL_FPE                  (8)
#define GDB_SIGNAL_SEGV                 (11)

static void gdb_parse_reset(mr_xvm_gdb_parse_t *parse) {
    /* Reset parse */
    parse->state = MR_XVM_GDB_PARSE_IDLE;
    parse->csum_n = 0;
    parse->len = 0;
}

static void gdb_reply_reset(mr_xvm_gdb_reply_t *reply) {
    /* Reset reply */
    reply->len = 0;
    reply->off = 0;
}

static mr_err_t gdb_init(mr_xvm_gdb_t *gdb, const mr_clazz_t *clazz,
                         const mr_xvm_gdb_ops_t *ops, void *priv) {
    /* Init GDB */
    gdb->state = MR_XVM_GDB_DETACHED;
    gdb_parse_reset(&gdb->parse);
    gdb_reply_reset(&gdb->reply);
    gdb->no_ack = MR_FALSE;
    gdb->xvm = MR_NULL;
    gdb->pc_nr = GDB_PC_NONE;
    gdb->ops = ops;
    gdb->priv = priv;

    /* Init object */
    return mr_object_init((mr_object_t *)gdb, clazz);
}

mr_err_t mr_xvm_gdb_init(mr_xvm_gdb_t *gdb, const mr_xvm_gdb_ops_t *ops,
                         void *priv) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(xvm_gdb);

    /* Check parameter */
    MR_ASSERT(gdb != MR_NULL);
    MR_ASSERT((ops != MR_NULL) && (ops->send != MR_NULL));

    /* Init GDB */
    return gdb_init(gdb, clazz, ops, priv);
}

mr_xvm_gdb_t *mr_xvm_gdb_create(const mr_xvm_gdb_ops_t *ops, void *priv) {
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(xvm_gdb);
    mr_xvm_gdb_t *gdb;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((ops != MR_NULL) && (ops->send != MR_NULL));

    /* Create GDB */
    gdb = mr_malloc(sizeof(mr_xvm_gdb_t));
    if (!gdb) {
        return MR_NULL;
    }

    /* Init GDB */
    ret = gdb_init(gdb, clazz, ops, priv);
    if (ret != 0) {
        mr_free(gdb);
        return MR_NULL;
    }
    return gdb;
}

static void gdb_detach(mr_xvm_gdb_t *gdb) {
    mr_xvm_t *xvm;

    /* Already detached */
    xvm = gdb->xvm;
    if (!xvm) {
        gdb->state = MR_XVM_GDB_DETACHED;
        gdb->pc_nr = GDB_PC_NONE;
        return;
    }

    /* Detach GDB */
    mr_xvm_debug_probe_detach(xvm);
    gdb->state = MR_XVM_GDB_DETACHED;
    gdb->xvm = MR_NULL;
    gdb->pc_nr = GDB_PC_NONE;
    mr_xvm_put(xvm);
}

mr_err_t mr_xvm_gdb_del(mr_xvm_gdb_t *gdb) {
    /* Check parameter */
    MR_ASSERT((gdb != MR_NULL) && MR_OBJECT_IS_INITED(gdb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(gdb, xvm_gdb));

    /* Detach the probe before delete */
    gdb_detach(gdb);

    /* Delete object */
    mr_object_del((mr_object_t *)gdb);
    return 0;
}

static mr_size_t gdb_reply_stop(char reply[4], unsigned int signal) {
    reply[0] = 'S';
    reply[1] = gdb_hex[(signal >> 4U) & 0x0fU];
    reply[2] = gdb_hex[signal & 0x0fU];
    reply[3] = '\0';
    return 3;
}

static unsigned int gdb_exception_signal(mr_uint32_t except_nr) {
    switch (except_nr) {
        case MR_XVM_EXCEPT_NR_BREAK: {
            return GDB_SIGNAL_TRAP;
        }
        case MR_XVM_EXCEPT_NR_ILLEGAL: {
            return GDB_SIGNAL_ILL;
        }
        case MR_XVM_EXCEPT_NR_DIV0: {
            return GDB_SIGNAL_FPE;
        }
        case MR_XVM_EXCEPT_NR_ACCESS:
        case MR_XVM_EXCEPT_NR_ALIGN: {
            return GDB_SIGNAL_SEGV;
        }
        default: {
            return GDB_SIGNAL_ABRT;
        }
    }
}

static mr_bool_t gdb_reply_put(mr_xvm_gdb_t *gdb, const void *buf,
                               mr_size_t len) {
    /* Check remaining reply space */
    if ((gdb->reply.len + len) > sizeof(gdb->reply.buf)) {
        return MR_FALSE;
    }

    /* Append to reply */
    mr_memcpy(gdb->reply.buf + gdb->reply.len, buf, len);
    gdb->reply.len += len;
    return MR_TRUE;
}

static mr_err_t gdb_reply_packet(mr_xvm_gdb_t *gdb, const char *payload,
                                 mr_size_t length) {
    mr_uint8_t checksum;
    char tail[3];
    mr_size_t i;

    /* Check remaining reply space */
    if ((sizeof(gdb->reply.buf) - gdb->reply.len)
        < (length + GDB_REPLY_OVERHEAD)) {
        return -MR_ENOMEM;
    }

    /* Build checksum */
    for (checksum = 0, i = 0; i < length; i++) {
        checksum = (mr_uint8_t)(checksum + (mr_uint8_t)payload[i]);
    }

    /* Append packet */
    if (!gdb_reply_put(gdb, "$", 1)) {
        return -MR_ENOMEM;
    }
    if ((length != 0) && (!gdb_reply_put(gdb, payload, length))) {
        return -MR_ENOMEM;
    }
    tail[0] = '#';
    tail[1] = gdb_hex[checksum >> 4];
    tail[2] = gdb_hex[checksum & 0x0f];
    if (!gdb_reply_put(gdb, tail, 3)) {
        return -MR_ENOMEM;
    }
    return 0;
}

static mr_err_t gdb_reply_kick(mr_xvm_gdb_t *gdb) {
    mr_ssize_t sent;
    mr_size_t left;

    /* Send pending reply */
    while (gdb->reply.off < gdb->reply.len) {
        left = gdb->reply.len - gdb->reply.off;
        sent = gdb->ops->send(gdb->priv, gdb->reply.buf + gdb->reply.off, left);
        if (sent < 0) {
            return -MR_EIO;
        }
        if (sent == 0) {
            return 0;
        }
        gdb->reply.off += (mr_size_t)sent;
    }

    /* Reset reply */
    gdb_reply_reset(&gdb->reply);
    if (gdb->ops->flush != MR_NULL) {
        return gdb->ops->flush(gdb->priv);
    }
    return 0;
}

static void gdb_probe_entry(void *priv, mr_xvm_return_t exec_ret,
                            mr_xvm_event_t *event) {
    mr_xvm_gdb_t *gdb;
    mr_size_t len;
    char reply[8];

    /* Get GDB session */
    gdb = (mr_xvm_gdb_t *)priv;

    /* Report a stop */
    if (exec_ret == MR_XVM_EXCEPT) {
        len = gdb_reply_stop(reply, gdb_exception_signal(event->except.nr));
        gdb_reply_packet(gdb, reply, len);
    } else {
        len = gdb_reply_stop(reply, GDB_SIGNAL_TRAP);
        gdb_reply_packet(gdb, reply, len);
    }
    gdb->state = MR_XVM_GDB_ATTACHED;
    gdb_reply_kick(gdb);
}

static mr_bool_t gdb_pc_nr(mr_xvm_isa_target_t *target, mr_uint32_t *nr) {
    mr_uint32_t i;

    /* Check target */
    if ((!target) || (!target->regs) || (target->regs_num == 0)) {
        return MR_FALSE;
    }

    /* Find PC by name */
    for (i = 0; i < target->regs_num; i++) {
        if (!target->regs[i].name) {
            continue;
        }
        if (mr_strcmp(target->regs[i].name, "pc") == 0) {
            *nr = i;
            return MR_TRUE;
        }
    }
    return MR_FALSE;
}

mr_err_t mr_xvm_gdb_attach(mr_xvm_gdb_t *gdb, mr_xvm_t *xvm) {
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((gdb != MR_NULL) && MR_OBJECT_IS_INITED(gdb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(gdb, xvm_gdb));
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));

    /* One XVM per session */
    if (gdb->xvm) {
        return -MR_EBUSY;
    }
    xvm = mr_xvm_get(xvm);
    if (!xvm) {
        return -MR_ENOENT;
    }

    /* Implant the probe */
    ret = mr_xvm_debug_probe_attach(xvm, gdb_probe_entry, gdb);
    if (ret != 0) {
        mr_xvm_put(xvm);
        return ret;
    }

    /* Attach GDB */
    gdb->state = MR_XVM_GDB_ATTACHED;
    gdb_parse_reset(&gdb->parse);
    gdb_reply_reset(&gdb->reply);
    gdb->no_ack = MR_FALSE;
    gdb->xvm = xvm;

    /* Find PC register number */
    if (!gdb_pc_nr(mr_xvm_debug_target(xvm), &gdb->pc_nr)) {
        gdb->pc_nr = GDB_PC_NONE;
    }
    return 0;
}

void mr_xvm_gdb_detach(mr_xvm_gdb_t *gdb) {
    /* Check parameter */
    MR_ASSERT((gdb != MR_NULL) && MR_OBJECT_IS_INITED(gdb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(gdb, xvm_gdb));

    /* Detach GDB */
    gdb_detach(gdb);
}

static mr_size_t gdb_reply_ok(char *reply) {
    mr_strcpy(reply, "OK");
    return 2;
}

static mr_size_t gdb_reply_err(char *reply) {
    mr_strcpy(reply, "E01");
    return 3;
}

static mr_size_t gdb_reply_text(char *reply, mr_size_t room, const char *text) {
    mr_size_t n;

    /* Copy a constant payload when it fits */
    n = mr_strlen(text);
    if ((n + 1U) > room) {
        return gdb_reply_err(reply);
    }
    mr_memcpy(reply, text, n + 1U);
    return n;
}

static int gdb_hex_value(char value) {
    if ((value >= '0') && (value <= '9')) {
        return value - '0';
    }
    if ((value >= 'a') && (value <= 'f')) {
        return value - 'a' + 10;
    }
    if ((value >= 'A') && (value <= 'F')) {
        return value - 'A' + 10;
    }
    return -1;
}

static mr_bool_t gdb_parse_hex(const char **text, mr_uint64_t *value) {
    mr_uint64_t result = 0;
    int digit;

    /* Need at least one hex digit */
    digit = gdb_hex_value(**text);
    if (digit < 0) {
        return MR_FALSE;
    }

    /* Consume the remaining hex digits */
    while ((digit = gdb_hex_value(**text)) >= 0) {
        result = (result << 4) | (mr_uint64_t)digit;
        (*text)++;
    }
    *value = result;
    return MR_TRUE;
}

static mr_bool_t gdb_parse_range(const char *text, mr_uint64_t *addr,
                                 mr_uint64_t *size, const char **end) {
    /* Parse addr,size */
    if ((!gdb_parse_hex(&text, addr)) || (*text++ != ',')
        || (!gdb_parse_hex(&text, size))) {
        return MR_FALSE;
    }

    /* Return the leftover text */
    if (end) {
        *end = text;
    }
    return MR_TRUE;
}

static mr_bool_t gdb_parse_nr(const char **text, mr_uint32_t *nr) {
    mr_uint64_t value;

    /* Parse a register number */
    if ((!gdb_parse_hex(text, &value)) || (value > MR_UINT32_MAX)) {
        return MR_FALSE;
    }
    *nr = (mr_uint32_t)value;
    return MR_TRUE;
}

static mr_xvm_isa_target_t *gdb_target(mr_xvm_gdb_t *gdb) {
    /* Get target kept by the debug unit */
    if ((!gdb) || (!gdb->xvm)) {
        return MR_NULL;
    }
    return mr_xvm_debug_target(gdb->xvm);
}

static void gdb_xml_win_init(gdb_xml_win_t *win, char *buf, mr_size_t cap,
                             mr_size_t offset, mr_size_t want) {
    win->offset = offset;
    win->want = want;
    win->buf = buf;
    win->cap = cap;
    win->pos = 0;
    win->len = 0;
}

static mr_bool_t gdb_xml_put(gdb_xml_win_t *win, const char *text) {
    mr_size_t n, start, count, avail;

    /* Count every byte; only store the requested window */
    n = mr_strlen(text);
    if ((win->pos + n) <= win->offset) {
        win->pos += n;
        return MR_TRUE;
    }
    start = 0;
    if (win->pos < win->offset) {
        start = win->offset - win->pos;
    }
    count = n - start;
    avail = 0;
    if (win->len < win->want) {
        avail = win->want - win->len;
    }
    if (avail > (win->cap - win->len)) {
        avail = win->cap - win->len;
    }
    if (count > avail) {
        count = avail;
    }
    if ((count > 0) && (win->buf != MR_NULL)) {
        mr_memcpy(win->buf + win->len, text + start, count);
        win->len += count;
    }
    win->pos += n;
    return MR_TRUE;
}

static mr_bool_t gdb_xml_put_u32(gdb_xml_win_t *win, mr_uint32_t value) {
    mr_size_t n, i;
    char tmp[11];
    char ch;

    /* Convert to a decimal string */
    if (value == 0) {
        tmp[0] = '0';
        tmp[1] = '\0';
        return gdb_xml_put(win, tmp);
    }
    n = 0;
    while (value != 0) {
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    tmp[n] = '\0';
    for (i = 0; i < (n / 2U); i++) {
        ch = tmp[i];
        tmp[i] = tmp[n - 1U - i];
        tmp[n - 1U - i] = ch;
    }
    return gdb_xml_put(win, tmp);
}

static const char *gdb_reg_type_name(mr_xvm_isa_reg_type_t type) {
    if (type == MR_XVM_ISA_REG_CODE) {
        return "code_ptr";
    }
    if (type == MR_XVM_ISA_REG_DATA) {
        return "data_ptr";
    }
    return "int";
}

static mr_bool_t gdb_build_target_xml(mr_xvm_isa_target_t *target,
                                      gdb_xml_win_t *win) {
    mr_xvm_isa_regdesc_t *reg;
    mr_uint32_t i;

    /* Check target */
    if ((!target) || ((!target->arch) || (target->arch[0] == '\0'))
        || ((!target->feature) || (target->feature[0] == '\0'))
        || ((!target->regs) || (target->regs_num == 0))) {
        return MR_FALSE;
    }

    /* Write header and architecture */
    gdb_xml_put(win, "<?xml version=\"1.0\"?>");
    gdb_xml_put(win, "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">");
    gdb_xml_put(win, "<target version=\"1.0\">");
    gdb_xml_put(win, "<architecture>");
    gdb_xml_put(win, target->arch);
    gdb_xml_put(win, "</architecture>");

    /* Write feature and registers */
    gdb_xml_put(win, "<feature name=\"");
    gdb_xml_put(win, target->feature);
    gdb_xml_put(win, "\">");
    for (i = 0; i < target->regs_num; i++) {
        reg = &target->regs[i];
        if ((!reg->name) || (reg->name[0] == '\0') || (reg->bit_size == 0)) {
            return MR_FALSE;
        }
        gdb_xml_put(win, "<reg name=\"");
        gdb_xml_put(win, reg->name);
        gdb_xml_put(win, "\" bitsize=\"");
        gdb_xml_put_u32(win, reg->bit_size);
        gdb_xml_put(win, "\" type=\"");
        gdb_xml_put(win, gdb_reg_type_name(reg->type));
        gdb_xml_put(win, "\" regnum=\"");
        gdb_xml_put_u32(win, i);
        gdb_xml_put(win, "\"/>");
    }

    /* Close feature and target */
    gdb_xml_put(win, "</feature>");
    gdb_xml_put(win, "</target>");
    return MR_TRUE;
}

static mr_size_t gdb_read_target_xml(mr_xvm_gdb_t *gdb, const char *packet,
                                     char *reply, mr_size_t room) {
    mr_uint64_t offset, size;
    mr_size_t cap, want;
    gdb_xml_win_t w;

    /* Parse the qXfer window first */
    if ((!gdb_parse_range(packet, &offset, &size, MR_NULL))
        || (offset > MR_SIZE_MAX) || (room < 2U)) {
        return gdb_reply_err(reply);
    }

    /* Store one slice after the m/l prefix */
    cap = room - 1U;
    want = (mr_size_t)size;
    if ((size > cap) || (want > cap)) {
        want = cap;
    }
    gdb_xml_win_init(&w, reply + 1, cap, (mr_size_t)offset, want);
    if (!gdb_build_target_xml(gdb_target(gdb), &w)) {
        return gdb_reply_err(reply);
    }

    /* Empty last slice when the offset is past the document */
    if (w.offset >= w.pos) {
        reply[0] = 'l';
        reply[1] = '\0';
        return 1;
    }
    reply[0] = (w.pos > (w.offset + w.len)) ? 'm' : 'l';
    reply[w.len + 1U] = '\0';
    return w.len + 1U;
}

static void gdb_put_hex_byte(char *output, mr_uint8_t byte) {
    output[0] = gdb_hex[byte >> 4];
    output[1] = gdb_hex[byte & 0x0f];
}

static mr_bool_t gdb_get_hex_byte(const char *input, mr_uint8_t *byte) {
    int high, low;

    high = gdb_hex_value(input[0]);
    low = gdb_hex_value(input[1]);
    if ((high < 0) || (low < 0)) {
        return MR_FALSE;
    }
    *byte = (mr_uint8_t)((high << 4) | low);
    return MR_TRUE;
}

static mr_size_t gdb_reg_bytes(const mr_xvm_isa_regdesc_t *reg) {
    /* Convert bit size to target bytes */
    if ((!reg) || (reg->bit_size > (GDB_REG_BYTES_MAX * 8U))
        || (reg->bit_size == 0)) {
        return 0;
    }
    return ((mr_size_t)reg->bit_size + 7U) / 8U;
}

static void gdb_encode_reg(char *output, mr_uint64_t value, mr_size_t bytes) {
    mr_size_t i;

    /* Encode little-endian hex */
    for (i = 0; i < bytes; i++) {
        gdb_put_hex_byte(output + (i * 2U), (mr_uint8_t)(value >> (i * 8U)));
    }
}

static mr_xvm_isa_regdesc_t *gdb_regdesc(mr_xvm_isa_target_t *target,
                                         mr_uint32_t nr) {
    /* Lookup register by number */
    if ((!target) || (!target->regs) || (nr >= target->regs_num)) {
        return MR_NULL;
    }
    return &target->regs[nr];
}

static mr_size_t gdb_read_registers(mr_xvm_gdb_t *gdb, char *reply,
                                    mr_size_t room) {
    mr_xvm_isa_target_t *target;
    mr_size_t bytes, off;
    mr_uint64_t value;
    mr_uint32_t nr;

    /* Get target */
    target = gdb_target(gdb);

    /* Check target */
    if ((!target) || (!target->regs) || (target->regs_num == 0)) {
        return gdb_reply_err(reply);
    }

    /* Read registers in table order */
    for (off = 0, nr = 0; nr < target->regs_num; nr++) {
        bytes = gdb_reg_bytes(&target->regs[nr]);
        if ((bytes == 0) || ((off + (bytes * 2U)) > room)
            || (mr_xvm_reg_load(gdb->xvm, nr, &value) != 0)) {
            return gdb_reply_err(reply);
        }
        gdb_encode_reg(reply + off, value, bytes);
        off += bytes * 2U;
    }
    reply[off] = '\0';
    return off;
}

static mr_bool_t gdb_decode_reg(const char *input, mr_uint64_t *value,
                                mr_size_t bytes) {
    mr_uint64_t result;
    mr_uint8_t byte;
    mr_size_t i;

    /* Decode little-endian hex */
    for (result = 0, i = 0; i < bytes; i++) {
        if (!gdb_get_hex_byte(input + (i * 2U), &byte)) {
            return MR_FALSE;
        }
        result |= (mr_uint64_t)byte << (i * 8U);
    }
    *value = result;
    return MR_TRUE;
}

static mr_size_t gdb_write_registers(mr_xvm_gdb_t *gdb, const char *packet,
                                     char *reply) {
    mr_size_t bytes, off, length;
    mr_xvm_isa_target_t *target;
    mr_uint64_t value;
    mr_uint32_t nr;

    /* Get target */
    target = gdb_target(gdb);

    /* Check target */
    if ((!target) || (!target->regs) || (target->regs_num == 0)) {
        return gdb_reply_err(reply);
    }

    /* Write registers in table order */
    length = mr_strlen(packet);
    for (off = 0, nr = 0; nr < target->regs_num; nr++) {
        bytes = gdb_reg_bytes(&target->regs[nr]);
        if ((bytes == 0) || ((off + (bytes * 2U)) > length)
            || (!gdb_decode_reg(packet + off, &value, bytes))
            || (mr_xvm_reg_store(gdb->xvm, nr, value) != 0)) {
            return gdb_reply_err(reply);
        }
        off += bytes * 2U;
    }
    if (off != length) {
        return gdb_reply_err(reply);
    }
    return gdb_reply_ok(reply);
}

static mr_size_t gdb_read_register(mr_xvm_gdb_t *gdb, const char *packet,
                                   char *reply, mr_size_t room) {
    mr_uint64_t value;
    mr_size_t bytes;
    mr_uint32_t nr;

    /* Read one register */
    if ((!gdb_parse_nr(&packet, &nr)) || (*packet != '\0')) {
        return gdb_reply_err(reply);
    }
    bytes = gdb_reg_bytes(gdb_regdesc(gdb_target(gdb), nr));
    if ((bytes == 0) || ((bytes * 2U) > room)
        || (mr_xvm_reg_load(gdb->xvm, nr, &value) != 0)) {
        return gdb_reply_err(reply);
    }
    gdb_encode_reg(reply, value, bytes);
    reply[bytes * 2U] = '\0';
    return bytes * 2U;
}

static mr_size_t gdb_write_register(mr_xvm_gdb_t *gdb, const char *packet,
                                    char *reply) {
    mr_uint64_t value;
    mr_size_t bytes;
    mr_uint32_t nr;

    /* Write one register */
    if ((!gdb_parse_nr(&packet, &nr)) || (*packet++ != '=')) {
        return gdb_reply_err(reply);
    }
    bytes = gdb_reg_bytes(gdb_regdesc(gdb_target(gdb), nr));
    if ((bytes == 0) || (mr_strlen(packet) != (bytes * 2U))
        || (!gdb_decode_reg(packet, &value, bytes))
        || (mr_xvm_reg_store(gdb->xvm, nr, value) != 0)) {
        return gdb_reply_err(reply);
    }
    return gdb_reply_ok(reply);
}

static mr_ssize_t gdb_datamem_span(mr_xvm_t *xvm, mr_uint64_t addr,
                                   mr_size_t left, void **mem,
                                   mr_bool_t *writable) {
    mr_ssize_t mapped;

    /* Map datamem */
    mapped = mr_xvm_datamem_map(xvm, addr, mem, writable);
    if ((mapped > 0) && ((mr_size_t)mapped > left)) {
        mapped = (mr_ssize_t)left;
    }
    return mapped;
}

static mr_size_t gdb_read_memory(mr_xvm_gdb_t *gdb, const char *packet,
                                 char *reply, mr_size_t room) {
    mr_uint64_t addr, size;
    mr_size_t done, i, n;
    mr_bool_t writable;
    mr_ssize_t chunk;
    mr_uint8_t *mem;

    /* Parse address and size */
    if ((!gdb_parse_range(packet, &addr, &size, MR_NULL))
        || (size > (room / 2U))) {
        return gdb_reply_err(reply);
    }

    /* Map and encode memory */
    for (n = (mr_size_t)size, done = 0; done < n; done += (mr_size_t)chunk) {
        chunk = gdb_datamem_span(gdb->xvm, addr + done, n - done, (void **)&mem,
                                 &writable);
        if (chunk <= 0) {
            return gdb_reply_err(reply);
        }
        for (i = 0; i < (mr_size_t)chunk; i++) {
            gdb_put_hex_byte(reply + ((done + i) * 2U), mem[i]);
        }
    }
    reply[n * 2U] = '\0';
    return n * 2U;
}

static mr_size_t gdb_write_memory(mr_xvm_gdb_t *gdb, const char *packet,
                                  char *reply) {
    mr_uint64_t addr, size;
    mr_uint8_t *mem, byte;
    mr_size_t done, i, n;
    mr_bool_t writable;
    mr_ssize_t chunk;
    const char *data;

    /* Parse address, size and payload */
    if ((!gdb_parse_range(packet, &addr, &size, &data)) || (*data++ != ':')) {
        return gdb_reply_err(reply);
    }
    n = mr_strlen(data);
    if ((n & 1U) || (size != (n / 2U))) {
        return gdb_reply_err(reply);
    }

    /* Map and decode memory */
    for (n = n / 2U, done = 0; done < n; done += (mr_size_t)chunk) {
        chunk = gdb_datamem_span(gdb->xvm, addr + done, n - done, (void **)&mem,
                                 &writable);
        if ((chunk <= 0) || (!writable)) {
            return gdb_reply_err(reply);
        }
        for (i = 0; i < (mr_size_t)chunk; i++) {
            if (!gdb_get_hex_byte(data + ((done + i) * 2U), &byte)) {
                return gdb_reply_err(reply);
            }
            mem[i] = byte;
        }
    }
    return gdb_reply_ok(reply);
}

static mr_size_t gdb_break_command(mr_xvm_gdb_t *gdb, const char *packet,
                                   char *reply) {
    mr_uint64_t addr, kind;
    mr_err_t ret;

    /* Only software/hardware breakpoints */
    if ((packet[1] != '0') && (packet[1] != '1')) {
        return 0;
    }
    if (packet[2] != ',') {
        return 0;
    }

    /* Parse breakpoint address */
    if (!gdb_parse_range(packet + 3, &addr, &kind, MR_NULL)) {
        return gdb_reply_err(reply);
    }

    /* Insert or remove breakpoint */
    if (packet[0] == 'Z') {
        ret = mr_xvm_debug_break_insert(gdb->xvm, addr);
    } else {
        ret = mr_xvm_debug_break_remove(gdb->xvm, addr);
        if (ret == (-MR_ENOENT)) {
            ret = 0;
        }
    }
    if (ret != 0) {
        return gdb_reply_err(reply);
    }
    return gdb_reply_ok(reply);
}

static gdb_cmd_t gdb_run(mr_xvm_gdb_t *gdb, mr_bool_t step, const char *addr,
                         char *reply, mr_size_t *reply_len) {
    mr_uint64_t value, pc;
    mr_uint32_t gate;

    /* Check cached PC register */
    if (gdb->pc_nr == GDB_PC_NONE) {
        *reply_len = gdb_reply_err(reply);
        return GDB_CMD_REPLY;
    }

    /* Optional start address becomes PC */
    if (addr && (*addr != '\0')) {
        if ((!gdb_parse_hex(&addr, &value)) || (*addr != '\0')
            || (mr_xvm_reg_store(gdb->xvm, gdb->pc_nr, value) != 0)) {
            *reply_len = gdb_reply_err(reply);
            return GDB_CMD_REPLY;
        }
    }

    /* Skip the current breakpoint and open the gate */
    if (mr_xvm_reg_load(gdb->xvm, gdb->pc_nr, &pc) != 0) {
        *reply_len = gdb_reply_err(reply);
        return GDB_CMD_REPLY;
    }
    mr_xvm_debug_break_skip(gdb->xvm, pc);
    gate = step ? MR_XVM_DEBUG_GATE_STEP : MR_XVM_DEBUG_GATE_OPEN;
    mr_xvm_debug_probe_set_gate(gdb->xvm, gate);
    gdb->state = MR_XVM_GDB_RUNNING;
    *reply_len = 0;
    return GDB_CMD_SILENT;
}

static mr_size_t gdb_q_supported(mr_xvm_gdb_t *gdb, char *reply,
                                 mr_size_t room) {
    const char format[] = "PacketSize=%zx%s;QStartNoAckMode+;vContSupported+";
    const char *xfer;
    mr_size_t size;
    int n;

    /* PacketSize is hex of the smaller payload buffer */
    size = sizeof(gdb->parse.buf);
    if (size > (sizeof(gdb->reply.buf) - GDB_REPLY_OVERHEAD)) {
        size = sizeof(gdb->reply.buf) - GDB_REPLY_OVERHEAD;
    }
    xfer = gdb_target(gdb) ? ";qXfer:features:read+" : "";
    n = mr_snprintf(reply, room, format, size, xfer);
    if ((n < 0) || (((mr_size_t)n + 1U) > room)) {
        return gdb_reply_err(reply);
    }
    return (mr_size_t)n;
}

static gdb_cmd_t gdb_q(mr_xvm_gdb_t *gdb, const char *packet, char *reply,
                       mr_size_t room, mr_size_t *reply_len) {
    const char xml_prefix[] = "qXfer:features:read:target.xml:";

    /* Query packets */
    if (mr_strncmp(packet, "qSupported", 10) == 0) {
        *reply_len = gdb_q_supported(gdb, reply, room);
        return GDB_CMD_REPLY;
    }
    if (mr_strncmp(packet, xml_prefix, sizeof(xml_prefix) - 1) == 0) {
        *reply_len = gdb_read_target_xml(gdb, packet + sizeof(xml_prefix) - 1,
                                         reply, room);
        return GDB_CMD_REPLY;
    }
    if (mr_strncmp(packet, "qSymbol", 7) == 0) {
        *reply_len = gdb_reply_ok(reply);
        return GDB_CMD_REPLY;
    }
    if (mr_strcmp(packet, "qC") == 0) {
        *reply_len = gdb_reply_text(reply, room, "QC1");
        return GDB_CMD_REPLY;
    }
    if (mr_strcmp(packet, "qfThreadInfo") == 0) {
        *reply_len = gdb_reply_text(reply, room, "m1");
        return GDB_CMD_REPLY;
    }
    if (mr_strcmp(packet, "qsThreadInfo") == 0) {
        *reply_len = gdb_reply_text(reply, room, "l");
        return GDB_CMD_REPLY;
    }
    if (mr_strncmp(packet, "qAttached", 9) == 0) {
        *reply_len = gdb_reply_text(reply, room, "1");
        return GDB_CMD_REPLY;
    }
    return GDB_CMD_REPLY;
}

static gdb_cmd_t gdb_Q(mr_xvm_gdb_t *gdb, const char *packet, char *reply,
                       mr_size_t *reply_len) {
    /* Set packets */
    if (mr_strcmp(packet, "QStartNoAckMode") == 0) {
        *reply_len = gdb_reply_ok(reply);
        gdb->no_ack = MR_TRUE;
    }
    return GDB_CMD_REPLY;
}

static gdb_cmd_t gdb_v(mr_xvm_gdb_t *gdb, const char *packet, char *reply,
                       mr_size_t room, mr_size_t *reply_len) {
    /* vCont packets */
    if (mr_strcmp(packet, "vCont?") == 0) {
        *reply_len = gdb_reply_text(reply, room, "vCont;c;s");
        return GDB_CMD_REPLY;
    }
    if (mr_strncmp(packet, "vCont;c", 7) == 0) {
        return gdb_run(gdb, MR_FALSE, MR_NULL, reply, reply_len);
    }
    if (mr_strncmp(packet, "vCont;s", 7) == 0) {
        return gdb_run(gdb, MR_TRUE, MR_NULL, reply, reply_len);
    }
    return GDB_CMD_REPLY;
}

static gdb_cmd_t gdb_command(mr_xvm_gdb_t *gdb, const char *packet, char *reply,
                             mr_size_t room, mr_size_t *reply_len) {
    /* Default empty reply */
    reply[0] = '\0';
    *reply_len = 0;

    /* Route by first character */
    switch (packet[0]) {
        case 'q': {
            return gdb_q(gdb, packet, reply, room, reply_len);
        }
        case 'Q': {
            return gdb_Q(gdb, packet, reply, reply_len);
        }
        case 'v': {
            return gdb_v(gdb, packet, reply, room, reply_len);
        }
        case '?': {
            if (mr_xvm_status(gdb->xvm) == MR_XVM_STATUS_ENDED) {
                *reply_len = gdb_reply_text(reply, room, "W00");
            } else {
                *reply_len = gdb_reply_stop(reply, GDB_SIGNAL_TRAP);
            }
            return GDB_CMD_REPLY;
        }
        case 'g': {
            *reply_len = gdb_read_registers(gdb, reply, room);
            return GDB_CMD_REPLY;
        }
        case 'G': {
            *reply_len = gdb_write_registers(gdb, packet + 1, reply);
            return GDB_CMD_REPLY;
        }
        case 'p': {
            *reply_len = gdb_read_register(gdb, packet + 1, reply, room);
            return GDB_CMD_REPLY;
        }
        case 'P': {
            *reply_len = gdb_write_register(gdb, packet + 1, reply);
            return GDB_CMD_REPLY;
        }
        case 'm': {
            *reply_len = gdb_read_memory(gdb, packet + 1, reply, room);
            return GDB_CMD_REPLY;
        }
        case 'M': {
            *reply_len = gdb_write_memory(gdb, packet + 1, reply);
            return GDB_CMD_REPLY;
        }
        case 'Z':
        case 'z': {
            *reply_len = gdb_break_command(gdb, packet, reply);
            return GDB_CMD_REPLY;
        }
        case 'c':
        case 's': {
            return gdb_run(gdb, packet[0] == 's', packet + 1, reply, reply_len);
        }
        case 'H': {
            *reply_len = gdb_reply_ok(reply);
            return GDB_CMD_REPLY;
        }
        case 'T': {
            if (mr_strcmp(packet, "T1") == 0) {
                *reply_len = gdb_reply_ok(reply);
            }
            return GDB_CMD_REPLY;
        }
        case 'D': {
            *reply_len = gdb_reply_ok(reply);
            return GDB_CMD_CLOSE;
        }
        case 'k': {
            return GDB_CMD_KILL;
        }
        default: {
            return GDB_CMD_REPLY;
        }
    }
}

static void gdb_close_session(mr_xvm_gdb_t *gdb) {
    void (*closed)(void *priv);
    void *priv;

    /* Save the hook before dropping the attach reference */
    closed = (gdb->ops != MR_NULL) ? gdb->ops->closed : MR_NULL;
    priv = gdb->priv;

    /* Detach GDB */
    gdb_detach(gdb);
    if (MR_OBJECT_IS_INITED(gdb)) {
        gdb->state = MR_XVM_GDB_CLOSED;
    }

    /* Notify closed */
    if (closed != MR_NULL) {
        closed(priv);
    }
}

static char *gdb_reply_begin(mr_xvm_gdb_t *gdb, mr_size_t *room) {
    mr_size_t left;

    /* Leave room for '$', "#xx" and a temporary NUL */
    left = sizeof(gdb->reply.buf) - gdb->reply.len;
    if (left < (GDB_REPLY_OVERHEAD + 4U)) {
        *room = 0;
        return MR_NULL;
    }

    /* Payload starts after '$' */
    gdb->reply.buf[gdb->reply.len] = '$';
    *room = left - GDB_REPLY_OVERHEAD;
    gdb->reply.buf[gdb->reply.len + 1U] = '\0';
    return gdb->reply.buf + gdb->reply.len + 1U;
}

static void gdb_reply_end(mr_xvm_gdb_t *gdb, mr_size_t length) {
    mr_uint8_t checksum;
    char *payload;
    mr_size_t i;

    /* Checksum the payload written after '$' */
    payload = gdb->reply.buf + gdb->reply.len + 1U;
    for (checksum = 0, i = 0; i < length; i++) {
        checksum = (mr_uint8_t)(checksum + (mr_uint8_t)payload[i]);
    }
    payload[length] = '#';
    payload[length + 1U] = gdb_hex[checksum >> 4];
    payload[length + 2U] = gdb_hex[checksum & 0x0f];
    gdb->reply.len += 1U + length + 3U;
}

static void gdb_dispatch(mr_xvm_gdb_t *gdb) {
    mr_size_t reply_len;
    mr_size_t room;
    gdb_cmd_t cmd;
    char *reply;

    /* Start the reply payload */
    gdb->parse.buf[gdb->parse.len] = '\0';
    reply = gdb_reply_begin(gdb, &room);
    if (!reply) {
        return;
    }

    /* Unattached sessions reply an error */
    if (!gdb->xvm) {
        gdb_reply_end(gdb, gdb_reply_err(reply));
        gdb_reply_kick(gdb);
        return;
    }

    /* Dispatch the command */
    cmd = gdb_command(gdb, gdb->parse.buf, reply, room, &reply_len);
    if ((cmd == GDB_CMD_REPLY) || (cmd == GDB_CMD_CLOSE)) {
        gdb_reply_end(gdb, reply_len);
    }

    /* Kick reply */
    gdb_reply_kick(gdb);
    if ((cmd == GDB_CMD_CLOSE) || (cmd == GDB_CMD_KILL)) {
        gdb_close_session(gdb);
    }
}

static void gdb_interrupt(mr_xvm_gdb_t *gdb) {
    mr_size_t len;
    char reply[4];

    /* Hold the target and report a stop */
    mr_xvm_debug_probe_set_gate(gdb->xvm, MR_XVM_DEBUG_GATE_HOLD);
    gdb->state = MR_XVM_GDB_ATTACHED;
    len = gdb_reply_stop(reply, GDB_SIGNAL_INT);
    gdb_reply_packet(gdb, reply, len);
    gdb_reply_kick(gdb);
}

static mr_err_t gdb_reply_ack(mr_xvm_gdb_t *gdb, char ack) {
    /* Skip ACK in no-ack mode */
    if (gdb->no_ack) {
        return 0;
    }
    return gdb_reply_put(gdb, &ack, 1) ? 0 : (-MR_ENOMEM);
}

static void gdb_parse_byte(mr_xvm_gdb_t *gdb, char value) {
    mr_uint8_t actual;
    mr_uint8_t expect;
    int high, low;
    mr_size_t i;

    switch (gdb->parse.state) {
        case MR_XVM_GDB_PARSE_IDLE: {
            /* Interrupt, ACK or start of packet */
            switch (value) {
                case 0x03: {
                    if (gdb->xvm) {
                        gdb_interrupt(gdb);
                    }
                    break;
                }
                case '+':
                case '-': {
                    break;
                }
                case '$': {
                    gdb->parse.state = MR_XVM_GDB_PARSE_DATA;
                    gdb->parse.csum_n = 0;
                    gdb->parse.len = 0;
                    break;
                }
                default: {
                    break;
                }
            }
            break;
        }
        case MR_XVM_GDB_PARSE_DATA: {
            /* Collect payload until '#' */
            if (value == '#') {
                gdb->parse.state = MR_XVM_GDB_PARSE_CKSUM;
                gdb->parse.csum_n = 0;
                break;
            }

            /* Packet overflowed the parse buffer, NAK and drop it */
            if ((gdb->parse.len + 1U) >= sizeof(gdb->parse.buf)) {
                gdb_reply_ack(gdb, '-');
                gdb_reply_kick(gdb);
                gdb_parse_reset(&gdb->parse);
                break;
            }
            gdb->parse.buf[gdb->parse.len++] = value;
            break;
        }
        case MR_XVM_GDB_PARSE_CKSUM: {
            /* Checksum digits */
            gdb->parse.csum[gdb->parse.csum_n++] = value;
            if (gdb->parse.csum_n < 2U) {
                break;
            }

            /* Verify checksum */
            for (actual = 0, i = 0; i < gdb->parse.len; i++) {
                actual = (mr_uint8_t)(actual + (mr_uint8_t)gdb->parse.buf[i]);
            }
            high = gdb_hex_value(gdb->parse.csum[0]);
            low = gdb_hex_value(gdb->parse.csum[1]);
            expect = (mr_uint8_t)((high << 4) | low);

            /* Bad checksum digits or mismatch, NAK and drop it */
            if ((high < 0) || (low < 0) || (actual != expect)) {
                gdb_reply_ack(gdb, '-');
                gdb_reply_kick(gdb);
                gdb_parse_reset(&gdb->parse);
                break;
            }

            /* Dispatch packet */
            gdb_reply_ack(gdb, '+');
            gdb_dispatch(gdb);
            gdb_parse_reset(&gdb->parse);
            break;
        }
        default: {
            break;
        }
    }
}

mr_ssize_t mr_xvm_gdb_recv(mr_xvm_gdb_t *gdb, const void *buf, mr_size_t len) {
    const char *bytes;
    mr_size_t i;

    /* Check parameter */
    MR_ASSERT((gdb != MR_NULL) && MR_OBJECT_IS_INITED(gdb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(gdb, xvm_gdb));
    MR_ASSERT((buf != MR_NULL) || (len == 0));

    /* Session already closed */
    if (gdb->state == MR_XVM_GDB_CLOSED) {
        return 0;
    }

    /* Parse received bytes */
    bytes = (const char *)buf;
    for (i = 0; i < len; i++) {
        gdb_parse_byte(gdb, bytes[i]);
        if (gdb->state == MR_XVM_GDB_CLOSED) {
            i++;
            break;
        }
    }
    return (mr_ssize_t)i;
}

mr_err_t mr_xvm_gdb_flush(mr_xvm_gdb_t *gdb) {
    /* Check parameter */
    MR_ASSERT((gdb != MR_NULL) && MR_OBJECT_IS_INITED(gdb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(gdb, xvm_gdb));

    /* Kick reply */
    return gdb_reply_kick(gdb);
}

mr_bool_t mr_xvm_gdb_want_send(const mr_xvm_gdb_t *gdb) {
    /* Check parameter */
    MR_ASSERT((gdb != MR_NULL) && MR_OBJECT_IS_INITED(gdb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(gdb, xvm_gdb));

    /* Reply still pending */
    return (gdb->reply.off < gdb->reply.len) ? MR_TRUE : MR_FALSE;
}

mr_xvm_gdb_state_t mr_xvm_gdb_state(const mr_xvm_gdb_t *gdb) {
    /* Check parameter */
    MR_ASSERT((gdb != MR_NULL) && MR_OBJECT_IS_INITED(gdb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(gdb, xvm_gdb));

    /* Get GDB state */
    return gdb->state;
}

static mr_xvm_gdb_t *gdb_obj_release(mr_object_t *obj) {
    mr_xvm_gdb_t *gdb;

    /* Release GDB */
    gdb = MR_CONTAINER_OF(obj, mr_xvm_gdb_t, parent);

    /* Detach GDB */
    gdb_detach(gdb);
    gdb->ops = MR_NULL;
    gdb->priv = MR_NULL;
    gdb_parse_reset(&gdb->parse);
    gdb_reply_reset(&gdb->reply);
    return gdb;
}

static void gdb_statik_release(mr_object_t *obj) {
    /* Release GDB */
    gdb_obj_release(obj);
}

static void gdb_dynamic_release(mr_object_t *obj) {
    mr_xvm_gdb_t *gdb;

    /* Release GDB */
    gdb = gdb_obj_release(obj);

    /* Free GDB */
    mr_free(gdb);
}
#endif /* defined(MR_USE_MODULE_XVM_GDB) */
