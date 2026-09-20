#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IDL Backend Schema - Compact runtime schema encoding and C blob emission.

Serializes the resolved semantic RPC schema, optionally compresses its body
with zlib, and emits both the stored bytes and the complete self-description RPC
call into generated `idl.c`.

The stored blob starts with `MRID` and one info byte. The high nibble of the info
byte is the format version and the low nibble is the compression method
(0 = none, 1 = zlib). The body then contains the UTF-8 string table, enum table,
struct table, and service/method table. Counts, indices, IDs, lengths, and
unsigned enum values use unsigned LEB128. Signed enum values use ZigZag +
unsigned LEB128. Field types are recursive bytecode so the runtime representation
stays independent from C packed-storage layout.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

import zlib

from ..semantic.model import (
    ArrayTypeRef,
    EnumTypeRef,
    Field,
    ResolvedType,
    ScalarType,
    Schema,
    StructTypeRef,
    VariableArrayTypeRef,
)
from .emit import CEmit


class SchemaEncoder:
    """Encodes a resolved semantic schema into a compact binary format."""

    MAGIC = b"MRID"
    FORMAT_VERSION = 2
    COMPRESSION_NONE = 0
    COMPRESSION_ZLIB = 1

    TYPE_SCALARS = {
        "u8": 0x00,
        "u16": 0x01,
        "u32": 0x02,
        "u64": 0x03,
        "i8": 0x04,
        "i16": 0x05,
        "i32": 0x06,
        "i64": 0x07,
        "bool": 0x08,
        "f32": 0x09,
        "f64": 0x0A,
    }
    TYPE_ENUM = 0x20
    TYPE_STRUCT = 0x21
    TYPE_ARRAY = 0x24
    TYPE_VARIABLE_ARRAY = 0x25

    def encode(self, schema: Schema) -> bytes:
        """Encode one semantic schema into the stored self-description blob."""
        strings = self._collect_strings(schema)
        string_ids = {value: index for index, value in enumerate(strings)}
        enum_ids = {name: index for index, name in enumerate(schema.enums)}
        struct_ids = {name: index for index, name in enumerate(schema.structs)}
        body = bytearray()

        self._write_strings(body, strings)
        self._write_enums(body, schema, string_ids)
        self._write_structs(body, schema, string_ids, enum_ids, struct_ids)
        self._write_services(body, schema, string_ids, enum_ids, struct_ids)

        raw_body = bytes(body)
        compressed = zlib.compress(raw_body, level=9)
        if len(compressed) < len(raw_body):
            payload = compressed
            compression = self.COMPRESSION_ZLIB
        else:
            payload = raw_body
            compression = self.COMPRESSION_NONE

        info = (self.FORMAT_VERSION << 4) | compression
        return self.MAGIC + bytes((info,)) + payload

    @staticmethod
    def _collect_strings(schema: Schema) -> list[str]:
        """Collect user-facing names once in deterministic declaration order."""
        strings: list[str] = []
        seen: set[str] = set()

        def add(value: str) -> None:
            if value not in seen:
                seen.add(value)
                strings.append(value)

        for enum in schema.enums.values():
            add(enum.name)
            for item in enum.items:
                add(item.name)
        for struct in schema.structs.values():
            add(struct.name)
            for field in struct.fields:
                add(field.name)
        for service in schema.services.values():
            add(service.name)
            for method in service.methods:
                add(method.name)
                for field in method.request:
                    add(field.name)
                for field in method.response:
                    add(field.name)
        return strings

    def _write_strings(self, output: bytearray, strings: list[str]) -> None:
        """Write the deduplicated UTF-8 string table."""
        self._uvar(output, len(strings))
        for value in strings:
            encoded = value.encode("utf-8")
            self._uvar(output, len(encoded))
            output.extend(encoded)

    def _write_enums(self, output: bytearray, schema: Schema,
                     string_ids: dict[str, int]) -> None:
        """Write enum declarations and assigned item values."""
        self._uvar(output, len(schema.enums))
        for enum in schema.enums.values():
            self._uvar(output, string_ids[enum.name])
            output.append(self.TYPE_SCALARS[enum.backing])
            self._uvar(output, len(enum.items))
            signed = enum.backing.startswith("i")
            for item in enum.items:
                self._uvar(output, string_ids[item.name])
                if signed:
                    self._svar(output, item.value)
                else:
                    self._uvar(output, item.value)

    def _write_structs(self, output: bytearray, schema: Schema,
                       string_ids: dict[str, int], enum_ids: dict[str, int],
                       struct_ids: dict[str, int]) -> None:
        """Write named structures and their fields."""
        self._uvar(output, len(schema.structs))
        for struct in schema.structs.values():
            self._uvar(output, string_ids[struct.name])
            self._write_fields(output, struct.fields, string_ids, enum_ids,
                               struct_ids)

    def _write_services(self, output: bytearray, schema: Schema,
                        string_ids: dict[str, int], enum_ids: dict[str, int],
                        struct_ids: dict[str, int]) -> None:
        """Write services, methods, and request/response field lists."""
        self._uvar(output, len(schema.services))
        for service in schema.services.values():
            self._uvar(output, string_ids[service.name])
            self._uvar(output, service.service_id)
            self._uvar(output, len(service.methods))
            for method in service.methods:
                self._uvar(output, string_ids[method.name])
                self._uvar(output, method.method_id)
                output.append(0x01 if method.oneway else 0x00)
                self._write_fields(output, method.request, string_ids,
                                   enum_ids, struct_ids)
                self._write_fields(output, method.response, string_ids,
                                   enum_ids, struct_ids)

    def _write_fields(self, output: bytearray, fields: list[Field],
                      string_ids: dict[str, int], enum_ids: dict[str, int],
                      struct_ids: dict[str, int]) -> None:
        """Write one field list using recursive type bytecode."""
        self._uvar(output, len(fields))
        for field in fields:
            self._uvar(output, string_ids[field.name])
            self._write_type(output, field.type, enum_ids, struct_ids)

    def _write_type(self, output: bytearray, resolved: ResolvedType,
                    enum_ids: dict[str, int],
                    struct_ids: dict[str, int]) -> None:
        """Write one resolved type expression."""
        if isinstance(resolved, ScalarType):
            output.append(self.TYPE_SCALARS[resolved.name])
            return
        if isinstance(resolved, EnumTypeRef):
            output.append(self.TYPE_ENUM)
            self._uvar(output, enum_ids[resolved.name])
            return
        if isinstance(resolved, StructTypeRef):
            output.append(self.TYPE_STRUCT)
            self._uvar(output, struct_ids[resolved.name])
            return
        if isinstance(resolved, ArrayTypeRef):
            output.append(self.TYPE_ARRAY)
            self._uvar(output, resolved.length)
            self._write_type(output, resolved.element, enum_ids, struct_ids)
            return
        if isinstance(resolved, VariableArrayTypeRef):
            output.append(self.TYPE_VARIABLE_ARRAY)
            self._uvar(output, resolved.max_count)
            self._write_type(output, resolved.element, enum_ids, struct_ids)
            return
        raise ValueError(f"unsupported resolved RPC IDL type: {resolved!r}")

    @staticmethod
    def _uvar(output: bytearray, value: int) -> None:
        """Write one unsigned LEB128 integer."""
        if value < 0:
            raise ValueError("unsigned varint cannot encode a negative value")
        while value >= 0x80:
            output.append((value & 0x7F) | 0x80)
            value >>= 7
        output.append(value)

    @classmethod
    def _svar(cls, output: bytearray, value: int) -> None:
        """Write one signed integer as ZigZag + unsigned LEB128."""
        encoded = (value << 1) if value >= 0 else ((-value << 1) - 1)
        cls._uvar(output, encoded)


class SchemaSourceBuilder:
    """Emits one stored schema blob into the generated C source."""

    BYTES_PER_LINE = 12

    def __init__(self) -> None:
        self._encoder = SchemaEncoder()

    def render(self, schema: Schema) -> list[str]:
        """Render the embedded description and its complete RPC call."""
        blob = self._encoder.encode(schema)
        lines = [
            CEmit.banner("self desc"),
            "#if defined(MR_USE_MODULE_RPC_SELF_DESC)",
            "static const mr_uint8_t __mr_rpc_self_desc[] = {",
        ]
        for offset in range(0, len(blob), self.BYTES_PER_LINE):
            chunk = blob[offset:offset + self.BYTES_PER_LINE]
            values = ", ".join(f"0x{value:02x}U" for value in chunk)
            lines.append(f"{CEmit.INDENT}{values},")
        lines.extend([
            "};",
            "",
            "static mr_err_t rpc_decode_self_desc(mr_rpc_session_t *session) {",
            (f"{CEmit.INDENT}if (session->request.type "
             "!= MR_RPC_MSG_REQUEST) {"),
            f"{CEmit.INDENT * 2}return -MR_EPERM;",
            f"{CEmit.INDENT}}}",
            (f"{CEmit.INDENT}if (session->recv.size "
             "!= sizeof(session->request.payload.__mr_rpc_self_desc)) {"),
            f"{CEmit.INDENT * 2}return -MR_EINVAL;",
            f"{CEmit.INDENT}}}",
            f"{CEmit.INDENT}return 0;",
            "}",
            "",
            "static mr_uint32_t rpc_self_desc_size(mr_uint32_t offset) {",
            f"{CEmit.INDENT}mr_uint32_t remain;",
            "",
            (f"{CEmit.INDENT}if (offset >= "
             "sizeof(__mr_rpc_self_desc)) {"),
            f"{CEmit.INDENT * 2}return 0U;",
            f"{CEmit.INDENT}}}",
            (f"{CEmit.INDENT}remain = "
             "(mr_uint32_t)sizeof(__mr_rpc_self_desc) - offset;"),
            (f"{CEmit.INDENT}if (remain > "
             "MR_CFG_RPC_SELF_DESC_CHUNK_SIZE) {"),
            (f"{CEmit.INDENT * 2}remain = "
             "MR_CFG_RPC_SELF_DESC_CHUNK_SIZE;"),
            f"{CEmit.INDENT}}}",
            f"{CEmit.INDENT}return remain;",
            "}",
            "",
            "static mr_err_t rpc_invoke_self_desc(mr_rpc_session_t *session) {",
            f"{CEmit.INDENT}mr_uint32_t offset, size;",
            "",
            (f"{CEmit.INDENT}offset = "
             "session->request.payload.__mr_rpc_self_desc;"),
            (f"{CEmit.INDENT}if (offset > "
             "sizeof(__mr_rpc_self_desc)) {"),
            f"{CEmit.INDENT * 2}return -MR_EINVAL;",
            f"{CEmit.INDENT}}}",
            f"{CEmit.INDENT}size = rpc_self_desc_size(offset);",
            f"{CEmit.INDENT}if (size != 0U) {{",
            (f"{CEmit.INDENT * 2}mr_memcpy("
             "session->response.payload.__mr_rpc_self_desc,"),
            ("                  &__mr_rpc_self_desc[offset], "
             "size);"),
            f"{CEmit.INDENT}}}",
            f"{CEmit.INDENT}return 0;",
            "}",
            "",
            "static mr_err_t rpc_encode_self_desc(mr_rpc_session_t *session) {",
            f"{CEmit.INDENT}mr_uint32_t offset;",
            "",
            (f"{CEmit.INDENT}offset = "
             "session->request.payload.__mr_rpc_self_desc;"),
            f"{CEmit.INDENT}return (mr_err_t)rpc_self_desc_size(offset);",
            "}",
            "",
            "MR_RPC_EXPORT(__mr_rpc_self_desc,",
            "              rpc_decode_self_desc,",
            "              rpc_invoke_self_desc,",
            "              rpc_encode_self_desc,",
            "              0xffffU,",
            "              0U);",
            "#endif /* defined(MR_USE_MODULE_RPC_SELF_DESC) */",
        ])
        return lines
