#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IDL Backend Header - C header emission for the RPC IDL compiler.

Renders interned layouts, user types, method payloads, overlay unions,
and `MR_RPC` user-entry macros into one generated header.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..semantic.model import (
    ArrayTypeRef,
    EnumTypeRef,
    InternedTypeRef,
    ResolvedType,
    InternedType,
    ScalarType,
    StructTypeRef,
    Method,
    Schema,
    Service,
    Field,
)
from .names import CNames
from .emit import CEmit
from pathlib import Path


class HeaderBuilder:
    """Renders a schema into a complete generated C header."""

    def build(self, schema: Schema, output_path: Path) -> str:
        """Render and wrap one header for `output_path`."""
        return self.wrap(output_path, self.render(schema))

    def render(self, schema: Schema) -> str:
        """Render the header body for one schema."""
        lines: list[str] = []
        self._emit_interned(lines, schema)
        self._emit_enums(lines, schema)
        self._emit_structs(lines, schema)
        self._emit_methods(lines, schema)
        self._emit_unions(lines, schema)
        self._emit_exports(lines, schema)
        return "\n".join(lines).rstrip() + "\n"

    @classmethod
    def wrap(cls, output_path: Path, header_text: str) -> str:
        """Wrap rendered types with the generated header scaffold."""
        guard = cls._header_guard(output_path)
        lines = [
            f"#ifndef {guard}",
            f"#define {guard}",
            "",
            CEmit.generated_at(),
            "",
            "#include <mrX/libc/mr_compiler.h>",
            "#include <mrX/libc/mr_types.h>",
            "#include <mrX/libc/mr_errno.h>",
            "",
            "struct mr_rpc_session;",
            "",
            "#ifdef __cplusplus",
            'extern "C" {',
            "#endif /* __cplusplus */",
            "",
        ]
        body = header_text.strip()
        if body:
            lines.extend([body, ""])
        lines.extend([
            "#ifdef __cplusplus",
            "}",
            "#endif /* __cplusplus */",
            "",
            f"#endif /* {guard} */",
            "",
        ])
        return "\n".join(lines)

    def _emit_interned(self, lines: list[str], schema: Schema) -> None:
        """Emit interned string and bytes layouts once."""
        interned = [*schema.strings.values(), *schema.bytes_types.values()]
        if not interned:
            return
        lines.append(CEmit.section_banner("intern"))
        for item in interned:
            self._emit_interned_type(lines, item)
            lines.append("")

    def _emit_interned_type(self, lines: list[str],
                            interned: InternedType) -> None:
        """Emit one interned string or bytes struct."""
        tag = CNames.intern_tag(interned.kind, interned.max_len)
        typedef = CNames.intern_typedef(interned.kind, interned.max_len)
        size_c = CNames.SCALARS[interned.size_type]
        if interned.kind == "string":
            data_type = "char"
            data_len = interned.max_len + 1
        else:
            data_type = "mr_uint8_t"
            data_len = interned.max_len
        self._emit_packed_struct(lines, tag, typedef, [
            f"{size_c} size",
            f"{data_type} data[{data_len}]",
        ])

    def _emit_enums(self, lines: list[str], schema: Schema) -> None:
        """Emit enum typedefs and item constants."""
        for enum in schema.enums.values():
            lines.append(CEmit.section_banner("enum", enum.name))
            backing = CNames.SCALARS[enum.backing]
            typedef = CNames.user_typedef(enum.name)
            lines.append(f"typedef {backing} {typedef};")
            macros = [
                (CNames.enum_const(enum.name, item.name),
                 CNames.enum_literal(enum.backing, item.value))
                for item in enum.items
            ]
            width = max((len(name) for name, _ in macros), default=0)
            for name, literal in macros:
                pad = " " * (width - len(name) + 4)
                lines.append(f"#define {name}{pad}({literal})")
            lines.append("")

    def _emit_structs(self, lines: list[str], schema: Schema) -> None:
        """Emit user structs in declaration order."""
        for struct in schema.structs.values():
            lines.append(CEmit.section_banner("struct", struct.name))
            tag = CNames.user_tag(struct.name)
            typedef = CNames.user_typedef(struct.name)
            self._emit_packed_struct(lines, tag, typedef,
                                     self._field_decls(struct.fields))
            lines.append("")

    def _emit_methods(self, lines: list[str], schema: Schema) -> None:
        """Emit per-method request and response structs with fields."""
        for service in schema.services.values():
            groups = self._method_payloads(service)
            if not groups:
                continue
            lines.append(CEmit.section_banner("service", service.name))
            for group in groups:
                for tag, typedef, fields in group:
                    self._emit_packed_struct(lines, tag, typedef, fields)
                lines.append("")

    def _method_payloads(self, service: Service) -> list[list[tuple[str, str,
    list[str]]]]:
        """Group request/response structs by method for one service."""
        groups: list[list[tuple[str, str, list[str]]]] = []
        for method in service.methods:
            group: list[tuple[str, str, list[str]]] = []
            request_fields = self._field_decls(method.request)
            if request_fields:
                group.append((
                    CNames.method_request_tag(service.name, method.name),
                    CNames.method_request_typedef(service.name, method.name),
                    request_fields,
                ))
            if not method.oneway:
                response_fields = self._field_decls(method.response)
                if response_fields:
                    group.append((
                        CNames.method_response_tag(service.name, method.name),
                        CNames.method_response_typedef(service.name,
                                                       method.name),
                        response_fields,
                    ))
            if group:
                groups.append(group)
        return groups

    def _emit_unions(self, lines: list[str], schema: Schema) -> None:
        """Emit the session request and response overlay unions."""
        lines.append(CEmit.section_banner("unions"))
        request_members: list[str] = []
        response_members: list[str] = []
        for service in schema.services.values():
            for method in service.methods:
                member = CNames.union_member(service.name, method.name)
                if method.request:
                    req = CNames.method_request_typedef(service.name,
                                                        method.name)
                    request_members.append(f"{req} {member}")
                if method.oneway or not method.response:
                    continue
                res = CNames.method_response_typedef(service.name, method.name)
                response_members.append(f"{res} {member}")
        self._emit_union(lines, "mr_rpc_idl_request", "mr_rpc_idl_request_t",
                         request_members)
        lines.append("")
        self._emit_union(lines, "mr_rpc_idl_response", "mr_rpc_idl_response_t",
                         response_members)
        lines.append("")

    def _emit_exports(self, lines: list[str], schema: Schema) -> None:
        """Emit `MR_RPC` expansion macros for each method."""
        for service in schema.services.values():
            if not service.methods:
                continue
            lines.append(CEmit.section_banner("export", service.name))
            for method in service.methods:
                lines.extend(self._export_macro_lines(service, method))
            lines.append("")

    def _export_macro_lines(self, service: Service,
                            method: Method) -> list[str]:
        """Render one `__MR_RPC_ENTRY_*` macro for `MR_RPC`."""
        macro = CNames.export_macro(service.name, method.name)
        decl = CEmit.handler_decl(service.name, method, indent="    ",
                                  width=CEmit.LINE_WIDTH - 1)
        lines = [CEmit.slash(f"#define {macro}")]
        lines.extend(CEmit.slash(line) for line in decl[:-1])
        lines.append(decl[-1])
        return lines

    def _emit_packed_struct(self, lines: list[str], tag: str, typedef: str,
                            fields: list[str]) -> None:
        """Emit one named packed struct typedef."""
        lines.append(f"typedef struct {tag} {{")
        for field in fields:
            lines.append(f"{CEmit.INDENT}{field};")
        lines.append(f"}} MR_PACKED {typedef};")

    def _emit_union(self, lines: list[str], tag: str, typedef: str,
                    members: list[str]) -> None:
        """Emit one overlay union, with a placeholder when empty."""
        lines.append(f"typedef union {tag} {{")
        if not members:
            lines.append(f"{CEmit.INDENT}mr_uint8_t unused;")
        else:
            for member in members:
                lines.append(f"{CEmit.INDENT}{member};")
        lines.append(f"}} {typedef};")

    def _field_decls(self, fields: list[Field]) -> list[str]:
        """Format struct fields as `type name[dims]` declarations."""
        decls: list[str] = []
        for field in fields:
            base, suffix = self._c_type(field.type)
            decls.append(f"{base} {field.name}{suffix}")
        return decls

    def _c_type(self, resolved: ResolvedType) -> tuple[str, str]:
        """Return a C type name and array suffix for one resolved type."""
        dims: list[int] = []
        current = resolved
        while isinstance(current, ArrayTypeRef):
            dims.append(current.length)
            current = current.element
        suffix = "".join(f"[{length}]" for length in dims)
        if isinstance(current, ScalarType):
            return CNames.SCALARS[current.name], suffix
        if isinstance(current, (EnumTypeRef, StructTypeRef)):
            return CNames.user_typedef(current.name), suffix
        if isinstance(current, InternedTypeRef):
            return CNames.intern_typedef(current.kind, current.max_len), suffix
        return "mr_uint8_t", suffix

    @staticmethod
    def _header_guard(output_path: Path) -> str:
        """Build an include guard macro name from the output file name."""
        name = output_path.name.upper()
        guard = "".join(char if char.isalnum() else "_" for char in name)
        return f"__{guard}__"
