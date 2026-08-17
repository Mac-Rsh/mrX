#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Parser - Recursive descent parser for Hardware Tree Compiler.

Builds syntax nodes from tokens and performs literal decoding that belongs to
the parsing stage rather than the lexer.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

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
    FloatValue,
    IdentifierValue,
    MemreserveDecl,
    NodeDecl,
    NumberValue,
    PropertyDecl,
    ReferenceValue,
    RootNode,
    SourceSpan,
    STATUS_DEFER,
    STATUS_EARLY,
    STATUS_LAST,
    STATUS_PRIO_PREFIX,
    StringValue,
    ValueExpr,
)


class Parser:
    """Parses tokens into HTC syntax tree nodes."""

    VALUE_START_TOKENS = (
        TokenType.STRING,
        TokenType.NUMBER,
        TokenType.FLOAT,
        TokenType.IDENTIFIER,
        TokenType.AMPERSAND,
        TokenType.LT,
        TokenType.LBRACKET,
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
            if next_token is not None and next_token.type == TokenType.IDENTIFIER \
                    and next_token.value == "dts-v1":
                self._advance()
                self._advance()
                self._expect(TokenType.SLASH, ErrorCode.EXPECTED_SLASH,
                             "expected '/' after 'dts-v1' in version tag")
                self._expect(TokenType.SEMICOLON, ErrorCode.EXPECTED_SEMICOLON,
                             "expected ';' after '/dts-v1/'")
                document = Document(dts_v1=True, span=next_token.span)

        # Top-level roots, memreserves, and delete-node directives
        while not self._check(TokenType.EOF):
            if not self._check(TokenType.SLASH):
                token = self._peek()
                self._error(ErrorCode.INVALID_NODE_DECL,
                            "expected '/', '/memreserve/', or '/delete-node/' at top level",
                            token.span if token else None)
                self._sync()
                continue

            # Root node: `/ { ... }`
            if self._peek(1) is not None and self._peek(
                    1).type == TokenType.LBRACE:
                self._advance()
                self._advance()
                self._parse_root_contents(document)
                continue

            # Named top-level directives: `/memreserve/`, `/delete-node/`
            if self._peek(1) is not None and self._peek(
                    1).type == TokenType.IDENTIFIER:
                self._advance()
                name = self._advance()
                self._expect(TokenType.SLASH, ErrorCode.EXPECTED_SLASH,
                             f"expected '/' after '{name.value}'")
                if name.value == "memreserve":
                    self._parse_memreserve(name, document)
                    continue
                if name.value == "delete-node":
                    self._parse_delete_node(name, document)
                    continue
                self._error(ErrorCode.INVALID_NODE_DECL,
                            f"unsupported top-level directive: /{name.value}/",
                            name.span)
                self._sync()
                continue

        return document

    def _parse_root_contents(self, document: Document) -> None:
        """Parse the inside of a `/ { ... }` root and append it to the document."""
        slash_span = self.tokens[self.pos - 2].span if self.pos >= 2 else None
        properties: list[PropertyDecl] = []
        deleted_properties: list[DeletePropertyDecl] = []
        children: list[NodeDecl] = []
        while not self._check(TokenType.RBRACE, TokenType.EOF):
            if self._starts_delete_property():
                deletion = self._parse_delete_property()
                if deletion is not None:
                    deleted_properties.append(deletion)
                continue

            if not self._check(TokenType.IDENTIFIER):
                token = self._peek()
                self._error(ErrorCode.INVALID_NODE_DECL,
                            "expected property or child node in root",
                            token.span if token else slash_span)
                if token is not None:
                    self._error(ErrorCode.EXPECTED_IDENTIFIER,
                                f"unexpected token '{token.value}' ({token.type.value})",
                                token.span)
                self._sync()
                continue

            if self._starts_property():
                prop = self._parse_property()
                if prop is not None:
                    properties.append(prop)
                continue

            node = self._parse_node()
            if node is not None:
                children.append(node)
        self._expect(TokenType.RBRACE, ErrorCode.EXPECTED_RBRACE,
                     "expected '}' to close root node")
        self._match(TokenType.SEMICOLON)
        document.roots.append(
            RootNode(properties=properties,
                     deleted_properties=deleted_properties,
                     children=children,
                     span=slash_span))

    def _parse_memreserve(self, name: Token,
                          document: Document) -> None:
        """Parse a `/memreserve/ <...>;` declaration."""
        values = self._parse_memreserve_values()
        self._expect(TokenType.SEMICOLON, ErrorCode.EXPECTED_SEMICOLON,
                     "expected ';' after '/memreserve/'")
        document.memreserves.append(
            MemreserveDecl(values=values, span=name.span))
        if self.strict:
            self._warn(
                ErrorCode.INVALID_NODE_DECL,
                "strict: /memreserve/ is parsed but ignored by the HTC macro backend",
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

    def _parse_delete_node(self, name: Token, document: Document) -> None:
        """Parse a `/delete-node/ name;` directive."""
        target = self._expect(TokenType.IDENTIFIER,
                              ErrorCode.EXPECTED_IDENTIFIER,
                              "expected node name after '/delete-node/'")
        self._expect(TokenType.SEMICOLON, ErrorCode.EXPECTED_SEMICOLON,
                     "expected ';' after '/delete-node/'")
        if target is not None:
            document.delete_nodes.append(
                DeleteNodeDecl(name=target.value, span=name.span))

    def _starts_delete_property(self) -> bool:
        """Return True when the current tokens start `/delete-property/ prop;`."""
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
                              "expected property name after '/delete-property/'")
        self._expect(TokenType.SEMICOLON, ErrorCode.EXPECTED_SEMICOLON,
                     "expected ';' after '/delete-property/'")
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
        """Parse a DTS node with optional label, unit address, and closing `;`."""
        if self._peek() is None:
            return None

        # Label
        label = self._parse_optional_label()

        # Name
        name_token = self._expect(TokenType.IDENTIFIER,
                                  ErrorCode.EXPECTED_IDENTIFIER,
                                  "expected node name")
        if name_token is None:
            self._sync()
            return None

        # Unit address: `@hex[,hex]*`; legacy `@0x...` accepted and kept as-is
        address_parts: list[str] = []
        if self._match(TokenType.AT):
            address_token = self._expect(TokenType.NUMBER,
                                         ErrorCode.EXPECTED_NUMBER,
                                         "expected address after '@'")
            if address_token is None:
                address_parts = []
            else:
                address_parts.append(address_token.value)
                while self._match(TokenType.COMMA):
                    part_token = self._expect(TokenType.NUMBER,
                                              ErrorCode.EXPECTED_NUMBER,
                                              "expected address after ',' in unit address")
                    if part_token is not None:
                        address_parts.append(part_token.value)
        address = ",".join(address_parts) if address_parts else None

        # Expect '{'
        self._expect(TokenType.LBRACE, ErrorCode.EXPECTED_LBRACE,
                     f"expected '{{' after node '{name_token.value}'")

        # Properties, child nodes, and delete-property directives
        properties: list[PropertyDecl] = []
        deleted_properties: list[DeletePropertyDecl] = []
        children: list[NodeDecl] = []
        while not self._check(TokenType.RBRACE, TokenType.EOF):
            if self._starts_delete_property():
                deletion = self._parse_delete_property()
                if deletion is not None:
                    deleted_properties.append(deletion)
                continue

            if not self._check(TokenType.IDENTIFIER):
                token = self._peek()
                self._error(ErrorCode.INVALID_NODE_DECL,
                            f"expected property or child node in '{name_token.value}'",
                            token.span if token else name_token.span)
                if token is not None:
                    self._error(ErrorCode.EXPECTED_IDENTIFIER,
                                f"unexpected token '{token.value}' ({token.type.value})",
                                token.span)
                self._sync()
                continue

            # Property
            if self._starts_property():
                prop = self._parse_property()
                if prop is not None:
                    properties.append(prop)
                continue

            # Child node
            child = self._parse_node()
            if child is not None:
                children.append(child)

        # Expect '}' and accept optional trailing ';'
        self._expect(TokenType.RBRACE, ErrorCode.EXPECTED_RBRACE,
                     f"expected '}}' to close node '{name_token.value}'")
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
            properties=properties,
            deleted_properties=deleted_properties,
            children=children,
            span=name_token.span,
        )

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
            self._expect(TokenType.SEMICOLON, ErrorCode.EXPECTED_SEMICOLON,
                         f"expected ';' after property '{name_token.value}'")
            return PropertyDecl(name=name_token.value,
                                value=EmptyValue(name_token.span),
                                span=name_token.span)

        # Assigned value, like: `status = "okay";`
        value = self._parse_value()
        self._expect(TokenType.SEMICOLON, ErrorCode.EXPECTED_SEMICOLON,
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
                       "number; use a cell list like '<...>' for DTS compatibility",
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
        """Parse either a scalar value or a multi-element value sequence."""
        first = self._parse_value_atom()
        if first is None:
            return None

        # Comma-separated list, like: `1, 2, 3`
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

        # Adjacent values, like: `<&uart0 1 2>` internals after atom parsing
        if self._check(*self.VALUE_START_TOKENS):
            elements = [first]
            while self._check(*self.VALUE_START_TOKENS):
                element = self._parse_value_atom()
                if element is None:
                    return None
                elements.append(element)
            if len(elements) > 1:
                return ArrayValue(elements=elements,
                                  mode=ArrayMode.EXPANDED,
                                  span=first.span)
        return first

    def _parse_value_atom(self) -> ValueExpr | None:
        """Parse one atomic value expression."""
        token = self._peek()
        if token is None:
            self._error(ErrorCode.EXPECTED_TOKEN, "expected value")
            return None

        # String
        if token.type == TokenType.STRING:
            self._advance()
            return StringValue(token.value, token.span)

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

        # Array
        if token.type == TokenType.LT:
            return self._parse_array()

        # Inline array
        if token.type == TokenType.LBRACKET:
            return self._parse_inline_array()

        # Unknown
        self._error(ErrorCode.EXPECTED_TOKEN,
                    f"expected value, found '{token.type.value}'", token.span)
        self._advance()
        return None

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
        address = self._expect(TokenType.NUMBER, ErrorCode.EXPECTED_NUMBER,
                               "expected address after '@' in reference")
        if address is None:
            return identifier.value
        return f"{identifier.value}@{address.value}"

    def _parse_number_value(self, token: Token) -> NumberValue | None:
        """Decode an integer literal token into a parsed number value."""
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

        # Convert to integer
        try:
            value = int(raw, base)
        except ValueError:
            self._error(ErrorCode.INVALID_NUMBER,
                        f"invalid {kind} literal", token.span)
            return None
        return NumberValue(value, raw, token.span)

    def _parse_float_value(self, token: Token) -> FloatValue | None:
        """Decode a floating-point literal token into a parsed float value."""
        try:
            value = float(token.value)
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
        while not self._check(TokenType.GT, TokenType.EOF):
            # Optional comma separators
            if self._match(TokenType.COMMA):
                continue
            element = self._parse_value_atom()
            if element is None:
                self._sync()
                break
            elements.append(element)

        # Expect '>'
        self._expect(TokenType.GT, ErrorCode.EXPECTED_GT,
                     "expected '>' to close array")
        return ArrayValue(elements=elements, mode=ArrayMode.EXPANDED,
                          span=lt.span if lt else None)

    def _parse_inline_array(self) -> ValueExpr | None:
        """Parse a bracketed value as a DTS bytestring or legacy inline array."""
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
                                f"invalid byte value in bytestring: '{token.value}'",
                                token.span)
                    break
                values.append(int(token.value, 16))
            self._expect(TokenType.RBRACKET, ErrorCode.EXPECTED_TOKEN,
                         "expected ']' to close bytestring")
            return ByteStringValue(
                values=values,
                original=" ".join(f"{value:02x}" for value in values),
                span=lbracket.span)

        # Legacy inline array: `[1, 2, 3]`
        elements: list[ValueExpr] = []
        while not self._check(TokenType.RBRACKET, TokenType.EOF):
            element = self._parse_value_atom()
            if element is None:
                self._sync()
                break
            elements.append(element)
            if not self._match(TokenType.COMMA):
                break

        # Expect ']'
        self._expect(TokenType.RBRACKET, ErrorCode.EXPECTED_TOKEN,
                     "expected ']' to close inline array")
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

    def _expect(self, token_type: TokenType, code: ErrorCode,
                message: str) -> Token | None:
        """Require a token type or emit a parser diagnostic."""
        token = self._peek()
        if token is not None and token.type == token_type:
            return self._advance()
        self._error(code, message, token.span if token else None)
        return None

    def _sync(self) -> None:
        """Recover parser state by skipping to a safe boundary token."""
        while not self._check(TokenType.SEMICOLON, TokenType.RBRACE,
                              TokenType.EOF):
            self._advance()
        if self._check(TokenType.SEMICOLON, TokenType.RBRACE):
            self._advance()

    def _error(self, code: ErrorCode, message: str,
               span: SourceSpan | None = None) -> None:
        """Emit a parser diagnostic when a sink is available."""
        if self.diagnostics is None:
            return
        diagnostic = self.diagnostics.error(code, message)
        if span is not None:
            diagnostic.with_source_span(span)
