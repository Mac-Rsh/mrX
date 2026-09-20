#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IDL Compiler - Programmatic compilation pipeline.

Runs preprocessing, lexing, parsing, semantic extraction, and C header
and source emission.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from dataclasses import dataclass
from pathlib import Path

from .semantic.model import (
    ArrayTypeRef,
    EnumTypeRef,
    VariableArrayTypeRef,
    ResolvedType,
    ScalarType,
    Schema,
    StructTypeRef,
)
from ..htc.common import CompilationUnit, Diagnostics, ErrorCode, SourceFile
from ..htc.frontend import CppOptions, CppPreprocessor, Preprocessor
from .backend import CSymbolChecker, HeaderBuilder, SourceBuilder
from .backend.layout import Layout, LayoutLimitError
from .frontend import Document, Lexer, Parser
from .semantic import SemanticBuilder


@dataclass(frozen=True)
class CompileOptions:
    """Inputs for one IDL compilation."""

    input_path: Path
    output_path: Path | None = None
    include_dirs: tuple[str, ...] = ()
    use_cpp: bool = True


@dataclass(frozen=True)
class CompileResult:
    """Result of one IDL compilation attempt."""

    diagnostics: Diagnostics
    header_text: str | None = None
    source_text: str | None = None
    unit: CompilationUnit | None = None
    document: Document | None = None
    schema: Schema | None = None

    @property
    def ok(self) -> bool:
        """Return True when header and source emission succeeded."""
        return (self.header_text is not None
                and self.source_text is not None
                and not self.diagnostics.has_errors())


class RpcIdlCompiler:
    """Runs RPC IDL preprocessing, parsing, semantic extraction, and C
    emission."""

    def compile(self, options: CompileOptions) -> CompileResult:
        """Compile one IDL source into generated C header and source text."""
        diagnostics = Diagnostics()

        # Preprocess, parse, then extract the schema
        unit = self._preprocess(options, diagnostics)
        if unit is None:
            return CompileResult(diagnostics=diagnostics)

        document = self._parse_document(unit, diagnostics)
        if document is None:
            return CompileResult(diagnostics=diagnostics, unit=unit)

        schema = SemanticBuilder(diagnostics).build(document)
        if diagnostics.has_errors() or schema is None:
            return CompileResult(diagnostics=diagnostics, unit=unit,
                                 document=document, schema=schema)

        # Bound expansion before any backend builds the payload layouts
        RpcIdlCompiler._check_codec_limits(schema, diagnostics)
        if diagnostics.has_errors():
            return CompileResult(diagnostics=diagnostics, unit=unit,
                                 document=document, schema=schema)

        # Check generated C identifiers after the schema is complete
        CSymbolChecker(diagnostics).check(schema)
        if diagnostics.has_errors():
            return CompileResult(diagnostics=diagnostics, unit=unit,
                                 document=document, schema=schema)

        # Render the generated header and source
        output_path = options.output_path or Path("mr_rpc_idl.h")
        header_text = HeaderBuilder().build(schema, output_path)
        source_text = SourceBuilder().build(schema)
        return CompileResult(diagnostics=diagnostics, header_text=header_text,
                             source_text=source_text, unit=unit,
                             document=document, schema=schema)

    @staticmethod
    def _parse_document(unit: CompilationUnit,
                        diagnostics: Diagnostics) -> Document | None:
        """Parse preprocessed sources into one IDL document."""
        files = RpcIdlCompiler._declaration_order(unit.files)
        document = Document()

        # Lex and parse each preprocessed file in declaration order
        for source_file in files:
            lexer = Lexer(source_file.text, source_file.path, diagnostics,
                          source_map=source_file.line_map)
            tokens = lexer.tokenize()
            parsed = Parser(tokens, diagnostics).parse()
            document.declarations.extend(parsed.declarations)
        if diagnostics.has_errors():
            return None
        return document

    @staticmethod
    def _check_codec_limits(schema: Schema, diagnostics: Diagnostics) -> None:
        """Reject oversized layouts before symbol checking or C emission."""
        for service in schema.services.values():
            for method in service.methods:
                payloads = [("request", method.request)]
                if not method.oneway:
                    payloads.append(("response", method.response))
                for which, fields in payloads:
                    try:
                        Layout.from_fields(fields, schema)
                    except LayoutLimitError as exc:
                        diagnostic = diagnostics.error(
                            ErrorCode.INVALID_ARRAY,
                            f"{which} of '{service.name}.{method.name}' {exc}",
                        )
                        if method.span is not None:
                            diagnostic.with_source_span(method.span)
                        diagnostic.add_note(
                            "fixed arrays containing variable-length fields "
                            "are expanded only up to the codec slot limit")

    @staticmethod
    def _declaration_order(files: list[SourceFile]) -> list[SourceFile]:
        """Order builtin-preprocessor files in include post-order.

        The C preprocessor inlines includes in place. The builtin fallback
        stores the entry file first and appends included files as they are
        scanned, so a parent can appear before files it depends on. Walk
        `included_from` so a file is parsed after everything it includes.
        """
        if len(files) <= 1:
            return files

        by_path = {source.path: source for source in files}
        children: dict[str, list[SourceFile]] = {
            source.path: [] for source in files
        }
        roots: list[SourceFile] = []
        for source in files:
            site = source.included_from
            if site is None or site.file_path not in by_path:
                roots.append(source)
                continue
            children[site.file_path].append(source)

        ordered: list[SourceFile] = []
        visited: set[str] = set()

        def visit(source: SourceFile) -> None:
            """Append one source after recursively visiting its includes."""
            if source.path in visited:
                return
            visited.add(source.path)
            for child in children[source.path]:
                visit(child)
            ordered.append(source)

        for root in roots:
            visit(root)
        for source in files:
            visit(source)
        return ordered

    @staticmethod
    def _preprocess(options: CompileOptions,
                    diagnostics: Diagnostics) -> CompilationUnit | None:
        """Preprocess one IDL source file into a compilation unit."""
        # Prefer an external C preprocessor
        if options.use_cpp:
            adapter = CppPreprocessor(diagnostics)
            cpp_options = CppOptions(include_dirs=options.include_dirs)
            unit = adapter.preprocess(str(options.input_path), cpp_options)
            if unit is not None:
                return CompilationUnit(
                    entry_path=str(options.input_path),
                    files=[unit.source_file],
                )
            if diagnostics.has_errors():
                return None

        # Fallback to the built-in limited preprocessor
        preprocessor = Preprocessor(
            [str(options.input_path.parent), *options.include_dirs],
            diagnostics,
        )
        unit = preprocessor.process(str(options.input_path))
        if diagnostics.has_errors():
            return None
        return unit

    @staticmethod
    def format_schema(schema: Schema) -> str:
        """Render the resolved schema for inspection before header emission."""
        lines = [
            "=== IDL schema ===",
            f"enum {len(schema.enums)}, struct {len(schema.structs)}, "
            f"service {len(schema.services)}",
            # Variable-array layouts
            f"variable array {len(schema.variable_arrays)}",
            f"payload max: request {schema.request_size_max}, "
            f"response {schema.response_size_max}",
        ]

        # Interned variable-array layouts
        if schema.variable_arrays:
            lines.append("")
            lines.append("[variable arrays]")
            for item in schema.variable_arrays.values():
                lines.append(
                    f"  #{item.index} "
                    f"{RpcIdlCompiler._format_type(item.element)}<{item.max_count}>  "
                    f"size={item.size_type}  packed={item.packed_size}")

        # User enums
        for enum in schema.enums.values():
            lines.append("")
            lines.append(f"[enum] {enum.name}: {enum.backing}  "
                         f"packed={enum.packed_size}")
            for item in enum.items:
                lines.append(f"  {item.name} = {item.value}")

        # User structs
        for struct in schema.structs.values():
            lines.append("")
            lines.append(f"[struct] {struct.name}  packed={struct.packed_size}")
            for field in struct.fields:
                lines.append(
                    f"  {field.name}: "
                    f"{RpcIdlCompiler._format_type(field.type)}  "
                    f"packed={field.type.packed_size}")

        # Services and methods
        for service in schema.services.values():
            lines.append("")
            lines.append(f"[service] {service.name}@{service.service_id}")
            for method in service.methods:
                kind = "ONEWAY" if method.oneway else "REQUEST"
                lines.append(
                    f"  {method.name}@{method.method_id} {kind}  "
                    f"request={method.request_size}  "
                    f"response={method.response_size}")
                lines.append("    request")
                RpcIdlCompiler._format_fields(lines, method.request, "    ")
                if method.oneway:
                    lines.append("    response (none)")
                    continue
                lines.append("    response")
                RpcIdlCompiler._format_fields(lines, method.response, "    ")
        return "\n".join(lines)

    @staticmethod
    def _format_fields(lines: list[str], fields, indent: str) -> None:
        """Append one field list, or `(empty)` when there are none."""
        if not fields:
            lines.append(f"{indent}  (empty)")
            return
        for field in fields:
            lines.append(
                f"{indent}  {field.name}: "
                f"{RpcIdlCompiler._format_type(field.type)}  "
                f"packed={field.type.packed_size}")

    @staticmethod
    def _format_type(resolved: ResolvedType) -> str:
        """Format a resolved type as IDL-like text."""
        if isinstance(resolved, ArrayTypeRef):
            return f"{RpcIdlCompiler._format_type(resolved.element)}" \
                   f"[{resolved.length}]"
        if isinstance(resolved, VariableArrayTypeRef):
            return f"{RpcIdlCompiler._format_type(resolved.element)}<{resolved.max_count}>"
        if isinstance(resolved, EnumTypeRef):
            return resolved.name
        if isinstance(resolved, StructTypeRef):
            return resolved.name
        if isinstance(resolved, ScalarType):
            return resolved.name
        return "?"