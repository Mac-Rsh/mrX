#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Parser - Recursive descent parser for Hardware Tree Compiler.

Builds syntax nodes from tokens and performs literal decoding that belongs to
the parsing stage rather than the lexer.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from dataclasses import dataclass, field
from .tokens import Token, TokenType
from ..common import (
    ArrayMode,
    ArrayValue,
    ByteStringValue,
    DeleteNodeDecl,
    DeletePropertyDecl,
    Diagnostics,
    Document,
    EmptyValue,
    ErrorCode,
    ExpressionValue,
    FloatValue,
    IdentifierValue,
    MemreserveDecl,
    NodeDecl,
    NodeOverrideDecl,
    NumberValue,
    PropertyDecl,
    ReferenceValue,
    RootNode,
    SourceLocation,
    SourceSpan,
    STATUS_DEFER,
    STATUS_EARLY,
    STATUS_LAST,
    STATUS_PRIO_PREFIX,
    StringValue,
    ValueExpr,
)


@dataclass
class _ParsedNodeBody:
    """Collect declarations shared by roots, nodes, and modifications."""

    properties: list[PropertyDecl] = field(default_factory=list)
    deleted_properties: list[DeletePropertyDecl] = field(default_factory=list)
    deleted_nodes: list[DeleteNodeDecl] = field(default_factory=list)
    children: list[NodeDecl] = field(default_factory=list)


class Parser:
    """Parses tokens into HTC syntax tree nodes."""

    VALUE_START_TOKENS = (
        TokenType.STRING,
        TokenType.CHAR,
        TokenType.NUMBER,
        TokenType.FLOAT,
        TokenType.IDENTIFIER,
        TokenType.AMPERSAND,
        TokenType.LT,
        TokenType.LBRACKET,
        TokenType.LPAREN,
    )

    def __init__(self, tokens: list[Token],
                 diagnostics: Diagnostics | None = None,
                 strict: bool = False) -> None:
        """Initialize parser state for one token stream."""
        self.tokens = tokens
        self.diagnostics = diagnostics
        self.pos = 0
        self.strict = strict

    def parse(self) -> Document:
        """Parse the full token stream into a DTS document."""
        return self._parse_document()

    def _parse_document(self) -> Document:
        """Parse an optional `/dts-v1/;` tag followed by top-level items."""
        document = Document()

        # Optional `/dts-v1/;` tag
        if self._check(TokenType.SLASH):
            next_token = self._peek(1)
            if (next_token is not None
                    and next_token.type == TokenType.IDENTIFIER
                    and next_token.value == "dts-v1"):
                self._advance()
                self._advance()
                self._expect(TokenType.SLASH, ErrorCode.EXPECTED_SLASH,
                             "expected '/' after 'dts-v1' in version tag")
                self._expect(TokenType.SEMICOLON, ErrorCode.EXPECTED_SEMICOLON,
                             "expected ';' after '/dts-v1/'")
                document = Document(dts_v1=True, span=next_token.span)

        # Top-level roots, modifications, memreserves, and delete directives
        while not self._check(TokenType.EOF):
            if self._check(TokenType.AMPERSAND):
                override = self._parse_node_override()
                if override is not None:
                    document.add_operation(override)
                continue

            if not self._check(TokenType.SLASH):
                token = self._peek()
                self._error(ErrorCode.INVALID_NODE_DECL,
                            "expected '/', '&label', or '&{/path}' "
                            "at top level",
                            token.span if token else None)
                if self._check(TokenType.RBRACE):
                    self._advance()
                else:
                    self._sync()
                continue

            # Root node: `/ { ... }`
            if self._peek(1) is not None and self._peek(
                    1).type == TokenType.LBRACE:
                self._advance()
                lbrace = self._advance()
                self._parse_root_contents(document, lbrace.span)
                continue

            # Named top-level directives: `/dts-v1/`, `/plugin/`,
            # `/memreserve/`, `/delete-node/`
            if self._peek(1) is not None and self._peek(
                    1).type == TokenType.IDENTIFIER:
                self._advance()
                name = self._advance()
                self._expect(TokenType.SLASH, ErrorCode.EXPECTED_SLASH,
                             f"expected '/' after '{name.value}'")
                if name.value == "dts-v1":
                    self._expect_semicolon(
                        "expected ';' after '/dts-v1/'")
                    if not document.dts_v1:
                        document = Document(
                            dts_v1=True,
                            memreserves=document.memreserves,
                            roots=document.roots,
                            delete_nodes=document.delete_nodes,
                            span=name.span,
                            overrides=document.overrides,
                            operations=document.operations,
                        )
                    continue
                if name.value == "plugin":
                    self._expect_semicolon(
                        "expected ';' after '/plugin/'")
                    continue
                if name.value == "memreserve":
                    self._parse_memreserve(name, document)
                    continue
                if name.value == "delete-node":
                    deletion = self._parse_delete_node_after_name(name)
                    if deletion is not None:
                        document.add_operation(deletion)
                    continue
                self._error(ErrorCode.INVALID_NODE_DECL,
                            f"unsupported top-level directive: /{name.value}/",
                            name.span)
                self._sync()
                continue

            token = self._peek()
            self._error(ErrorCode.INVALID_NODE_DECL,
                        "expected '{' or directive name after '/'",
                        token.span if token else None)
            self._advance()
            self._sync()

        return document

    def _parse_root_contents(
            self,
            document: Document,
            lbrace_span: SourceSpan | None = None,
    ) -> None:
        """Parse `/ { ... }` contents and append them to the document."""
        slash_span = self.tokens[self.pos - 2].span if self.pos >= 2 else None
        body = self._parse_node_body("root", slash_span)
        if self._expect(TokenType.RBRACE, ErrorCode.EXPECTED_RBRACE,
                        "expected '}' to close root node") is None:
            self._attach_opening_note("{", lbrace_span)
        self._match(TokenType.SEMICOLON)
        root = RootNode(properties=body.properties,
                        deleted_properties=body.deleted_properties,
                        deleted_nodes=body.deleted_nodes,
                        children=body.children,
                        span=slash_span)
        document.add_operation(root)

    def _parse_memreserve(self, name: Token,
                          document: Document) -> None:
        """Parse a `/memreserve/ <...>;` declaration."""
        values = self._parse_memreserve_values()
        self._expect_semicolon("expected ';' after '/memreserve/'")
        document.memreserves.append(
            MemreserveDecl(values=values, span=name.span))
        if self.strict:
            self._warn(
                ErrorCode.INVALID_NODE_DECL,
                "strict: /memreserve/ is parsed but ignored by the HTC "
                "macro backend",
                name.span,
            )

    def _parse_memreserve_values(self) -> list[ValueExpr]:
        """Parse the cell list inside a `/memreserve/` declaration."""
        values: list[ValueExpr] = []
        while not self._check(TokenType.SEMICOLON, TokenType.EOF):
            if self._match(TokenType.COMMA):
                continue
            value = self._parse_value_atom()
            if value is None:
                self._sync()
                break
            values.append(value)
        return values

    def _parse_delete_node(self) -> DeleteNodeDecl | None:
        """Parse a complete `/delete-node/ target;` directive."""
        slash = self._expect(TokenType.SLASH, ErrorCode.EXPECTED_SLASH,
                             "expected '/' before 'delete-node'")
        name = self._expect(TokenType.IDENTIFIER,
                            ErrorCode.EXPECTED_IDENTIFIER,
                            "expected 'delete-node' after '/'")
        self._expect(TokenType.SLASH, ErrorCode.EXPECTED_SLASH,
                     "expected '/' after 'delete-node'")
        if name is None or name.value != "delete-node":
            self._error(ErrorCode.INVALID_NODE_DECL,
                        "expected '/delete-node/' directive",
                        name.span if name is not None else None)
            return None
        return self._parse_delete_node_target(slash or name)

    def _parse_delete_node_after_name(
            self, name: Token) -> DeleteNodeDecl | None:
        """Parse a delete-node target after its directive name."""
        return self._parse_delete_node_target(name)

    def _parse_delete_node_target(
            self, directive: Token) -> DeleteNodeDecl | None:
        """Parse a name, reference, or absolute-path deletion target."""
        target: str | None = None
        is_reference = False
        is_path = False

        if self._check(TokenType.AMPERSAND):
            reference = self._parse_reference()
            if reference is not None:
                target = reference.reference
                is_reference = True
                is_path = reference.is_path
        elif self._check(TokenType.SLASH):
            target = self._parse_absolute_path()
            is_path = True
        elif self._check(TokenType.IDENTIFIER):
            identifier = self._advance()
            target = self._finish_reference_segment(identifier)
        else:
            token = self._peek()
            self._error(ErrorCode.EXPECTED_IDENTIFIER,
                        "expected node name or reference after '/delete-node/'",
                        token.span if token is not None else directive.span)

        self._expect_semicolon("expected ';' after '/delete-node/'")
        if target is None:
            return None
        return DeleteNodeDecl(target=target, is_reference=is_reference,
                              is_path=is_path, span=directive.span)

    def _parse_absolute_path(self) -> str | None:
        """Parse an absolute node path used as a deletion target."""
        self._expect(TokenType.SLASH, ErrorCode.EXPECTED_SLASH,
                     "expected '/' to start absolute path")
        parts = [""]
        while not self._check(TokenType.SEMICOLON, TokenType.EOF):
            segment = self._parse_reference_segment(
                "expected node name in absolute path")
            if segment is None:
                return None
            parts.append(segment)
            if not self._match(TokenType.SLASH):
                break
        return "/" if len(parts) == 1 else "/".join(parts)

    def _starts_omit_if_no_ref(self) -> bool:
        """Return True when the current tokens start `/omit-if-no-ref/`."""
        return self._check(TokenType.SLASH) \
            and self._peek(1) is not None \
            and self._peek(1).type == TokenType.IDENTIFIER \
            and self._peek(1).value == "omit-if-no-ref" \
            and self._peek(2) is not None \
            and self._peek(2).type == TokenType.SLASH

    def _starts_delete_node(self) -> bool:
        """Return True when the current tokens start `/delete-node/`."""
        return self._check(TokenType.SLASH) \
            and self._peek(1) is not None \
            and self._peek(1).type == TokenType.IDENTIFIER \
            and self._peek(1).value == "delete-node" \
            and self._peek(2) is not None \
            and self._peek(2).type == TokenType.SLASH

    def _starts_delete_property(self) -> bool:
        """Return True when tokens start `/delete-property/ prop;`."""
        current = self._peek()
        next_token = self._peek(1)
        if current is None:
            return False
        if current.type == TokenType.IDENTIFIER:
            return current.value == "delete-property" \
                and next_token is not None \
                and next_token.type == TokenType.SLASH
        if current.type == TokenType.SLASH:
            return next_token is not None \
                and next_token.type == TokenType.IDENTIFIER \
                and next_token.value == "delete-property" \
                and self._peek(2) is not None \
                and self._peek(2).type == TokenType.SLASH
        return False

    def _parse_delete_property(self) -> DeletePropertyDecl | None:
        """Parse a `/delete-property/ prop;` directive inside a node."""
        span = self._peek().span if self._peek() is not None else None
        legacy_form = not self._check(TokenType.SLASH)

        # Accept both DTS `/delete-property/ prop;` and legacy
        # `delete-property/ prop;` forms.
        if not legacy_form:
            self._advance()
            self._expect(TokenType.IDENTIFIER, ErrorCode.EXPECTED_IDENTIFIER,
                         "expected 'delete-property' after '/'")
        else:
            self._expect(TokenType.IDENTIFIER, ErrorCode.EXPECTED_IDENTIFIER,
                         "expected 'delete-property'")
        self._expect(TokenType.SLASH, ErrorCode.EXPECTED_SLASH,
                     "expected '/' after 'delete-property'")
        target = self._expect(TokenType.IDENTIFIER,
                              ErrorCode.EXPECTED_IDENTIFIER,
                              "expected property name after "
                              "'/delete-property/'")
        self._expect_semicolon("expected ';' after '/delete-property/'")
        if self.strict and legacy_form:
            self._warn(
                ErrorCode.INVALID_NODE_DECL,
                "strict: legacy 'delete-property/ prop;' should use "
                "'/delete-property/ prop;'",
                span,
            )
        if target is None:
            return None
        return DeletePropertyDecl(name=target.value, span=span)

    def _parse_node(self) -> NodeDecl | None:
        """Parse a DTS node with optional labels, unit address, and `;`."""
        if self._peek() is None:
            return None

        # Zero or more labels and optional `/omit-if-no-ref/` qualifiers
        labels: list[str] = []
        while True:
            if self._starts_omit_if_no_ref():
                self._advance()
                self._advance()
                self._advance()
                continue
            parsed_label = self._parse_optional_label()
            if parsed_label is not None:
                labels.append(parsed_label)
                continue
            break
        label = labels[0] if labels else None

        # Name
        name_token = self._expect(TokenType.IDENTIFIER,
                                  ErrorCode.EXPECTED_IDENTIFIER,
                                  "expected node name")
        if name_token is None:
            self._sync()
            return None

        # DTS unit address: one or more hexadecimal components without `0x`
        address_parts: list[str] = []
        if self._match(TokenType.AT):
            address = self._parse_unit_address(
                "expected hexadecimal address after '@'")
            if address is not None:
                address_parts.append(address)
                while self._match(TokenType.COMMA):
                    part = self._parse_unit_address(
                        "expected hexadecimal address after ',' in "
                        "unit address")
                    if part is not None:
                        address_parts.append(part)
        address = ",".join(address_parts) if address_parts else None

        # Expect '{'
        lbrace = self._expect(TokenType.LBRACE, ErrorCode.EXPECTED_LBRACE,
                              f"expected '{{' after node '{name_token.value}'")
        if lbrace is None:
            self._sync()
            return None

        body = self._parse_node_body(
            f"node '{name_token.value}'", name_token.span)

        # Expect '}' and accept optional trailing ';'
        if self._expect(TokenType.RBRACE, ErrorCode.EXPECTED_RBRACE,
                        f"expected '}}' to close node "
                        f"'{name_token.value}'") is None:
            self._attach_opening_note("{", lbrace.span)
        has_semicolon = self._match(TokenType.SEMICOLON)
        if self.strict and not has_semicolon:
            self._warn(
                ErrorCode.INVALID_NODE_DECL,
                f"strict: node '{name_token.value}' should end with '}};'",
                name_token.span,
            )
        return NodeDecl(
            name=name_token.value,
            label=label,
            address=address,
            properties=body.properties,
            deleted_properties=body.deleted_properties,
            deleted_nodes=body.deleted_nodes,
            children=body.children,
            span=name_token.span,
            labels=tuple(labels),
        )

    def _parse_node_override(self) -> NodeOverrideDecl | None:
        """Parse a top-level `&label { ... };` node modification."""
        target = self._parse_reference()
        if target is None:
            self._sync()
            return None
        lbrace = self._expect(TokenType.LBRACE, ErrorCode.EXPECTED_LBRACE,
                              "expected '{' after node reference")
        if lbrace is None:
            self._sync()
            return None

        body = self._parse_node_body("node modification", target.span)

        if self._expect(TokenType.RBRACE, ErrorCode.EXPECTED_RBRACE,
                        "expected '}' to close node modification") is None:
            self._attach_opening_note("{", lbrace.span)
        has_semicolon = self._match(TokenType.SEMICOLON)
        if self.strict and not has_semicolon:
            self._warn(
                ErrorCode.INVALID_NODE_DECL,
                "strict: node modification should end with '};'",
                target.span,
            )
        return NodeOverrideDecl(
            target=target,
            properties=body.properties,
            deleted_properties=body.deleted_properties,
            deleted_nodes=body.deleted_nodes,
            children=body.children,
            span=target.span,
        )

    def _parse_node_body(
            self,
            context: str,
            fallback_span: SourceSpan | None,
    ) -> _ParsedNodeBody:
        """Parse declarations shared by roots, nodes, and modifications."""
        body = _ParsedNodeBody()
        while not self._check(TokenType.RBRACE, TokenType.EOF):
            if self._starts_delete_property():
                deletion = self._parse_delete_property()
                if deletion is not None:
                    body.deleted_properties.append(deletion)
                continue
            if self._starts_delete_node():
                deletion = self._parse_delete_node()
                if deletion is not None:
                    body.deleted_nodes.append(deletion)
                continue
            if self._starts_omit_if_no_ref():
                child = self._parse_node()
                if child is not None:
                    body.children.append(child)
                continue
            if not self._check(TokenType.IDENTIFIER):
                token = self._peek()
                if token is not None:
                    self._error(
                        ErrorCode.INVALID_NODE_DECL,
                        f"unexpected token '{token.value}' in {context}; "
                        "expected property, child node, or delete directive",
                        token.span,
                    )
                else:
                    self._error(
                        ErrorCode.INVALID_NODE_DECL,
                        f"expected property, child node, or delete directive "
                        f"in {context}",
                        fallback_span,
                    )
                self._sync()
                continue
            if self._starts_property():
                prop = self._parse_property()
                if prop is not None:
                    body.properties.append(prop)
                continue
            child = self._parse_node()
            if child is not None:
                body.children.append(child)
        return body

    def _parse_property(self) -> PropertyDecl | None:
        """Parse a property declaration with optional strict-mode warnings."""
        # Property name
        name_token = self._expect(TokenType.IDENTIFIER,
                                  ErrorCode.EXPECTED_IDENTIFIER,
                                  "expected property name")
        if name_token is None:
            return None

        # Bare property, like: `status;`
        if not self._match(TokenType.ASSIGN):
            self._expect_semicolon(
                f"expected ';' after property '{name_token.value}'")
            return PropertyDecl(name=name_token.value,
                                value=EmptyValue(name_token.span),
                                span=name_token.span)

        # Assigned value, like: `status = "okay";`
        value = self._parse_value()
        self._expect_semicolon(
            f"expected ';' after value for '{name_token.value}'")
        if value is None:
            return None
        if self.strict:
            self._warn_strict_value(name_token, value)
            if name_token.value == "status" and isinstance(value, StringValue):
                self._warn_strict_status(name_token, value)
        return PropertyDecl(name=name_token.value, value=value,
                            span=name_token.span)

    def _warn_strict_status(self, name_token: Token,
                            value: StringValue) -> None:
        """Warn when a status value is an HTC extension in strict mode."""
        status = value.value
        if status in {STATUS_EARLY, STATUS_LAST, STATUS_DEFER} \
                or status.startswith(STATUS_PRIO_PREFIX):
            self._warn(
                ErrorCode.INVALID_STATUS,
                f"strict: status '{status}' is an HTC extension; "
                "use 'okay' or 'disabled' for DTS compatibility",
                value.span,
            )

    def _warn_strict_value(self, name_token: Token,
                           value: ValueExpr) -> None:
        """Warn when a property value is an HTC extension in strict mode."""
        if isinstance(value, NumberValue):
            self._warn(ErrorCode.INVALID_PROPERTY,
                       f"strict: property '{name_token.value}' uses a bare "
                       "number; use a cell list like '<...>' for DTS "
                       "compatibility",
                       value.span)
            return
        if isinstance(value, FloatValue):
            self._warn(ErrorCode.INVALID_PROPERTY,
                       f"strict: property '{name_token.value}' uses a float "
                       "value, which is not valid Linux DTS",
                       value.span)
            return
        if isinstance(value, IdentifierValue):
            self._warn(ErrorCode.INVALID_PROPERTY,
                       f"strict: property '{name_token.value}' uses a bare "
                       "identifier; use a string or a reference",
                       value.span)
            return

    def _warn(self, code: ErrorCode, message: str,
              span: SourceSpan | None = None) -> None:
        """Emit a parser warning when a sink is available."""
        if self.diagnostics is None:
            return
        diagnostic = self.diagnostics.warning(code, message)
        if span is not None:
            diagnostic.with_source_span(span)

    def _parse_value(self) -> ValueExpr | None:
        """Parse either a scalar value or a comma-separated value sequence."""
        first = self._parse_value_atom()
        if first is None:
            return None

        # Comma-separated list, like: `1, 2, 3` or `<&a 1>, <&b 2>`
        if self._match(TokenType.COMMA):
            elements = [first]
            while True:
                element = self._parse_value_atom()
                if element is None:
                    return None
                elements.append(element)
                if not self._match(TokenType.COMMA):
                    break
            return ArrayValue(elements=elements, mode=ArrayMode.EXPANDED,
                              span=first.span)

        return first

    def _parse_value_atom(self) -> ValueExpr | None:
        """Parse one atomic value expression."""
        token = self._peek()
        if token is None:
            self._error(ErrorCode.EXPECTED_TOKEN, "expected value")
            return None

        # String (with C/DTS adjacent string literal concatenation)
        if token.type == TokenType.STRING:
            self._advance()
            parts = [token.value]
            end_loc = token.span.end
            while self._check(TokenType.STRING):
                extra = self._advance()
                parts.append(extra.value)
                end_loc = extra.span.end
            span = SourceSpan(token.span.start, end_loc)
            return StringValue("".join(parts), span)

        # Character literal
        if token.type == TokenType.CHAR:
            self._advance()
            return self._parse_char_value(token)

        # Number
        if token.type == TokenType.NUMBER:
            self._advance()
            return self._parse_number_value(token)

        # Float
        if token.type == TokenType.FLOAT:
            self._advance()
            return self._parse_float_value(token)

        # Identifier
        if token.type == TokenType.IDENTIFIER:
            self._advance()
            return IdentifierValue(token.value, token.span)

        # Reference
        if token.type == TokenType.AMPERSAND:
            return self._parse_reference()

        # DTS `/bits/ <width> <...>` cell array
        if (token.type == TokenType.SLASH
                and self._peek(1) is not None
                and self._peek(1).type == TokenType.IDENTIFIER
                and self._peek(1).value == "bits"
                and self._peek(2) is not None
                and self._peek(2).type == TokenType.SLASH):
            return self._parse_bits_array()

        # Parenthesized C/DTS expression, like: `(1 << 4)` or `(FLAG_A | FLAG_B)`
        if token.type == TokenType.LPAREN:
            return self._parse_paren_expression()

        # Array
        if token.type == TokenType.LT:
            return self._parse_array()

        # Inline array / DTS bytestring
        if token.type == TokenType.LBRACKET:
            return self._parse_inline_array()

        # Unknown
        self._error(ErrorCode.EXPECTED_TOKEN,
                    f"expected value, found '{token.type.value}'", token.span)
        self._advance()
        return None

    def _parse_bits_array(self) -> ArrayValue | None:
        """Parse a DTS `/bits/ <width> <...>` cell array."""
        self._advance()  # '/'
        self._advance()  # 'bits'
        self._advance()  # '/'
        width = self._expect(TokenType.NUMBER, ErrorCode.EXPECTED_NUMBER,
                             "expected bit width (8, 16, 32, or 64) after "
                             "'/bits/'")
        if width is not None and width.value not in {"8", "16", "32", "64"}:
            self._error(ErrorCode.INVALID_NUMBER,
                        f"invalid /bits/ width '{width.value}'; expected 8, "
                        "16, 32, or 64",
                        width.span)
        return self._parse_array()

    def _parse_paren_expression(self) -> ExpressionValue | None:
        """Parse a parenthesized C/DTS expression preserving C syntax."""
        lparen = self._expect(TokenType.LPAREN, ErrorCode.EXPECTED_TOKEN,
                              "expected '(' to start expression")
        if lparen is None:
            return None

        depth = 1
        tokens: list[Token] = [lparen]
        while not self._check(TokenType.EOF) and depth > 0:
            if self._check(TokenType.SEMICOLON, TokenType.RBRACE):
                break
            token = self._advance()
            tokens.append(token)
            if token.type == TokenType.LPAREN:
                depth += 1
            elif token.type == TokenType.RPAREN:
                depth -= 1

        if depth > 0:
            self._error(ErrorCode.EXPECTED_TOKEN,
                        "expected ')' to close expression",
                        self._eof_or_prev_span())
            self._attach_opening_note("(", lparen.span)
            return None

        rendered = self._render_expression_tokens(tokens)
        span = SourceSpan(lparen.span.start, tokens[-1].span.end)
        return ExpressionValue(text=rendered, span=span)

    @staticmethod
    def _render_expression_tokens(tokens: list[Token]) -> str:
        """Render expression tokens back into valid C expression text."""
        parts: list[str] = []
        value_types = {
            TokenType.NUMBER,
            TokenType.FLOAT,
            TokenType.CHAR,
            TokenType.IDENTIFIER,
            TokenType.RPAREN,
        }
        compound_pairs = {
            ("<", "<"),
            (">", ">"),
            ("<", "="),
            (">", "="),
            ("=", "="),
            ("!", "="),
            ("&", "&"),
            ("|", "|"),
        }
        no_space_after = {TokenType.LPAREN, TokenType.TILDE, TokenType.BANG}
        no_space_before = {TokenType.RPAREN, TokenType.COMMA}

        prev: Token | None = None
        for token in tokens:
            text = f'"{token.value}"' if token.type == TokenType.STRING \
                else token.value
            if prev is None:
                parts.append(text)
                prev = token
                continue

            # Split `1-1` (lexed as NUMBER `1`, NUMBER `-1`) into `1 - 1`
            if (prev.type in value_types
                    and token.type in {TokenType.NUMBER, TokenType.FLOAT}
                    and token.value.startswith("-")):
                parts.append(" - ")
                parts.append(token.value[1:])
                prev = token
                continue

            # Keep two-character operators (`<<`, `>>`, `<=`, `&&`, `||`, ...)
            # joined when they were adjacent in source.
            if ((prev.value, token.value) in compound_pairs
                    and Parser._tokens_are_adjacent(prev, token)):
                parts.append(text)
                prev = token
                continue

            if prev.type in no_space_after or token.type in no_space_before:
                parts.append(text)
            else:
                parts.append(" ")
                parts.append(text)
            prev = token

        return "".join(parts)

    def _parse_char_value(self, token: Token) -> NumberValue:
        """Decode a single-quoted character literal into a numeric value."""
        inner = token.value[1:-1]
        escapes = {
            "\\n": "\n",
            "\\r": "\r",
            "\\t": "\t",
            "\\0": "\0",
            "\\a": "\a",
            "\\b": "\b",
            "\\f": "\f",
            "\\v": "\v",
            "\\\\": "\\",
            "\\'": "'",
            '\\"': '"',
        }
        if inner in escapes:
            code = ord(escapes[inner])
        elif inner.startswith("\\x"):
            code = int(inner[2:], 16)
        elif len(inner) == 1:
            code = ord(inner)
        else:
            code = 0
        return NumberValue(code, token.value, token.span)

    def _parse_reference(self) -> ReferenceValue | None:
        """Parse a label reference or absolute path reference."""
        # Expect '&'
        amp = self._expect(TokenType.AMPERSAND, ErrorCode.EXPECTED_AMPERSAND,
                           "expected '&'")
        if amp is None:
            return None

        # Path reference, like: `&{/soc/uart0}`
        if self._match(TokenType.LBRACE):
            self._expect(TokenType.SLASH, ErrorCode.EXPECTED_SLASH,
                         "expected '/' after '{' in path reference")
            parts = ["/"]

            # Path body
            while not self._check(TokenType.RBRACE, TokenType.EOF):
                if self._match(TokenType.SLASH):
                    parts.append("/")
                    continue
                segment = self._parse_reference_segment(
                    "expected identifier in path reference")
                if segment is None:
                    return None
                parts.append(segment)

            # Expect '}'
            self._expect(TokenType.RBRACE, ErrorCode.EXPECTED_RBRACE,
                         "expected '}' to close path reference")
            return ReferenceValue(reference="".join(parts), is_path=True,
                                  span=amp.span)

        # Label or shorthand path reference, like: `&uart0`
        identifier = self._expect(TokenType.IDENTIFIER,
                                  ErrorCode.EXPECTED_IDENTIFIER,
                                  "expected identifier after '&'")
        if identifier is None:
            return None

        # Like: `&uart/uart0`
        first_segment = self._finish_reference_segment(identifier)
        parts = [first_segment]
        while self._match(TokenType.SLASH):
            segment = self._parse_reference_segment(
                "expected identifier after '/' in reference")
            if segment is None:
                return None
            parts.append(segment)

        if len(parts) > 1 or "@" in first_segment:
            return ReferenceValue(reference="/" + "/".join(parts),
                                  is_path=True, span=amp.span)
        return ReferenceValue(reference=first_segment, is_path=False,
                              span=amp.span)

    def _parse_reference_segment(self, message: str) -> str | None:
        """Parse one node-name segment in a reference path."""
        identifier = self._expect(TokenType.IDENTIFIER,
                                  ErrorCode.EXPECTED_IDENTIFIER, message)
        if identifier is None:
            return None
        return self._finish_reference_segment(identifier)

    def _finish_reference_segment(self, identifier: Token) -> str:
        """Append an optional unit address to a parsed reference segment."""
        if not self._match(TokenType.AT):
            return identifier.value
        address = self._parse_unit_address(
            "expected hexadecimal address after '@' in reference")
        if address is None:
            return identifier.value
        address_parts = [address]
        while self._match(TokenType.COMMA):
            part = self._parse_unit_address(
                "expected hexadecimal address after ',' in unit address")
            if part is not None:
                address_parts.append(part)
        return f"{identifier.value}@{','.join(address_parts)}"

    def _parse_unit_address(self, message: str) -> str | None:
        """Parse one bare hexadecimal DTS unit-address component."""
        token = self._peek()
        address_types = {
            TokenType.IDENTIFIER,
            TokenType.NUMBER,
            TokenType.FLOAT,
        }
        if token is None or token.type not in address_types:
            self._error(ErrorCode.EXPECTED_NUMBER, message,
                        token.span if token is not None else None)
            return None

        tokens = [self._advance()]
        while self._check(*address_types):
            next_token = self._peek()
            if next_token is None \
                    or not self._tokens_are_adjacent(tokens[-1], next_token):
                break
            tokens.append(self._advance())

        raw = "".join(part.value for part in tokens)
        if not raw or not all(char in "0123456789abcdefABCDEF"
                              for char in raw):
            self._error(
                ErrorCode.INVALID_NUMBER,
                f"invalid unit address '{raw}'; expected hexadecimal digits "
                "without a prefix",
                tokens[0].span,
            )
            return None
        return raw

    @staticmethod
    def _tokens_are_adjacent(left: Token, right: Token) -> bool:
        """Return True when two tokens touch in the original source."""
        end = left.span.end
        return end is not None \
            and end.file_path == right.span.file_path \
            and end.line == right.span.line \
            and end.column == right.span.column

    def _parse_number_value(self, token: Token) -> NumberValue | None:
        """Decode an integer literal token (with optional C suffixes)."""
        raw = token.value
        lowered = raw.lower()
        kind = "decimal"

        # Detect literal base
        if lowered.startswith("0x") or lowered.startswith("-0x"):
            base = 16
            kind = "hexadecimal"
        elif lowered.startswith("0b") or lowered.startswith("-0b"):
            base = 2
            kind = "binary"
        elif lowered.startswith("0o") or lowered.startswith("-0o"):
            base = 8
            kind = "octal"
        else:
            base = 10

        # Strip optional C integer suffixes (U, L, UL, LU, LL, ULL, LLU)
        cleaned = raw
        suffix = lowered
        for candidate in ("ull", "llu", "ul", "lu", "ll", "u", "l"):
            if suffix.endswith(candidate):
                cleaned = raw[:-len(candidate)]
                break

        # Convert to integer
        try:
            value = int(cleaned, base)
        except ValueError:
            self._error(ErrorCode.INVALID_NUMBER,
                        f"invalid {kind} literal", token.span)
            return None
        return NumberValue(value, raw, token.span)

    def _parse_float_value(self, token: Token) -> FloatValue | None:
        """Decode a floating-point literal token (with optional C suffixes)."""
        cleaned = token.value.rstrip("fFlL")
        try:
            value = float(cleaned)
        except ValueError:
            self._error(ErrorCode.INVALID_NUMBER, "invalid float literal",
                        token.span)
            return None
        return FloatValue(value, token.value, token.span)

    def _parse_array(self) -> ArrayValue | None:
        """Parse an angle-bracket array value."""
        # Expect '<'
        lt = self._expect(TokenType.LT, ErrorCode.EXPECTED_LT,
                          "expected '<' for array")

        # Elements
        elements: list[ValueExpr] = []
        while not self._check(TokenType.GT, TokenType.SEMICOLON,
                              TokenType.RBRACE, TokenType.EOF):
            # Optional comma separators
            if self._match(TokenType.COMMA):
                continue
            element = self._parse_value_atom()
            if element is None:
                self._sync()
                break
            elements.append(element)

        # Expect '>'
        if self._expect(TokenType.GT, ErrorCode.EXPECTED_GT,
                        "expected '>' to close array") is None:
            self._attach_opening_note("<", lt.span if lt else None)
        return ArrayValue(elements=elements, mode=ArrayMode.EXPANDED,
                          span=lt.span if lt else None)

    def _parse_inline_array(self) -> ValueExpr | None:
        """Parse a bracketed DTS bytestring or legacy inline array."""
        # Expect '['
        lbracket = self._expect(TokenType.LBRACKET, ErrorCode.EXPECTED_TOKEN,
                                "expected '[' for bracketed value")
        if lbracket is None:
            return None

        # DTS bytestring: `[00 11 ff]` (hex byte pairs without commas)
        if self._starts_dts_bytestring():
            values: list[int] = []
            while self._check(TokenType.NUMBER, TokenType.IDENTIFIER):
                token = self._advance()
                if not self._is_hex_byte(token.value):
                    self._error(ErrorCode.INVALID_NUMBER,
                                "invalid byte value in bytestring: "
                                f"'{token.value}'",
                                token.span)
                    break
                values.append(int(token.value, 16))
            if self._expect(TokenType.RBRACKET, ErrorCode.EXPECTED_TOKEN,
                            "expected ']' to close bytestring") is None:
                self._attach_opening_note("[", lbracket.span)
            return ByteStringValue(
                values=values,
                original=" ".join(f"{value:02x}" for value in values),
                span=lbracket.span)

        # Legacy inline array: `[1, 2, 3]`
        elements: list[ValueExpr] = []
        while not self._check(TokenType.RBRACKET, TokenType.SEMICOLON,
                              TokenType.RBRACE, TokenType.EOF):
            element = self._parse_value_atom()
            if element is None:
                self._sync()
                break
            elements.append(element)
            if not self._match(TokenType.COMMA):
                break

        # Expect ']'
        if self._expect(TokenType.RBRACKET, ErrorCode.EXPECTED_TOKEN,
                        "expected ']' to close inline array") is None:
            self._attach_opening_note("[", lbracket.span)
        return ArrayValue(elements=elements, mode=ArrayMode.INLINE,
                          span=lbracket.span)

    def _starts_dts_bytestring(self) -> bool:
        """Return True when the bracket content looks like a DTS bytestring."""
        token = self._peek()
        if token is None:
            return False
        if token.type not in {TokenType.NUMBER, TokenType.IDENTIFIER}:
            return False
        return self._is_hex_byte(token.value)

    @staticmethod
    def _is_hex_byte(raw: str) -> bool:
        """Return True when raw is exactly two hexadecimal digits."""
        return len(raw) == 2 and all(
            char in "0123456789abcdefABCDEF" for char in raw)

    def _parse_optional_label(self) -> str | None:
        """Parse and return an optional leading node label."""
        colon = self._peek(1)
        if not self._check(TokenType.IDENTIFIER):
            return None
        if colon is None or colon.type != TokenType.COLON:
            return None
        label = self._advance().value
        self._advance()
        return label

    def _starts_property(self) -> bool:
        """Return True when the current token sequence starts a property."""
        next_token = self._peek(1)
        if next_token is None:
            return False
        return next_token.type in {TokenType.ASSIGN, TokenType.SEMICOLON}

    def _peek(self, offset: int = 0) -> Token | None:
        """Peek a token by relative offset."""
        index = self.pos + offset
        if index >= len(self.tokens):
            return None
        return self.tokens[index]

    def _advance(self) -> Token:
        """Consume and return the current token."""
        token = self.tokens[self.pos]
        self.pos += 1
        return token

    def _check(self, *token_types: TokenType) -> bool:
        """Return True when the current token matches any given type."""
        token = self._peek()
        return token is not None and token.type in token_types

    def _match(self, token_type: TokenType) -> bool:
        """Consume the current token when it matches the expected type."""
        if self._check(token_type):
            self._advance()
            return True
        return False

    def _prev_token(self) -> Token | None:
        """Return the most recently consumed token, if any."""
        if self.pos <= 0 or self.pos - 1 >= len(self.tokens):
            return None
        return self.tokens[self.pos - 1]

    def _eof_or_prev_span(self) -> SourceSpan | None:
        """Return a span at the end of the previous token when available."""
        prev = self._prev_token()
        if prev is not None:
            loc = prev.span.end or prev.span.start
            return SourceSpan(loc)
        token = self._peek()
        return token.span if token is not None else None

    def _expect_semicolon(self, message: str) -> Token | None:
        """Require `;`, pinning missing-semicolon errors to the prior line."""
        token = self._peek()
        if token is not None and token.type == TokenType.SEMICOLON:
            return self._advance()
        prev = self._prev_token()
        span: SourceSpan | None
        if (prev is not None
                and (token is None
                     or token.type == TokenType.EOF
                     or token.span.file_path != prev.span.file_path
                     or token.span.line != prev.span.line)):
            anchor = prev.span.end or SourceLocation(
                prev.span.file_path, prev.span.line, prev.span.column + 1)
            span = SourceSpan(anchor)
        else:
            span = token.span if token is not None else None
        self._error(ErrorCode.EXPECTED_SEMICOLON, message, span)
        return None

    def _attach_opening_note(self, opener: str,
                             span: SourceSpan | None) -> None:
        """Attach a note pointing to the unmatched opening delimiter."""
        if self.diagnostics is None or span is None:
            return
        if not self.diagnostics.diagnostics:
            return
        self.diagnostics.diagnostics[-1].add_note(
            f"opening '{opener}' was at {span}")

    def _expect(self, token_type: TokenType, code: ErrorCode,
                message: str) -> Token | None:
        """Require a token type or emit a parser diagnostic."""
        token = self._peek()
        if token is not None and token.type == token_type:
            return self._advance()
        self._error(code, message, token.span if token else None)
        return None

    def _sync(self) -> None:
        """Recover parser state without consuming an enclosing `}`."""
        while not self._check(TokenType.SEMICOLON, TokenType.RBRACE,
                              TokenType.EOF):
            self._advance()
        if self._check(TokenType.SEMICOLON):
            self._advance()

    def _error(self, code: ErrorCode, message: str,
               span: SourceSpan | None = None) -> None:
        """Emit a parser diagnostic when a sink is available."""
        if self.diagnostics is None:
            return
        diagnostic = self.diagnostics.error(code, message)
        if span is not None:
            diagnostic.with_source_span(span)
