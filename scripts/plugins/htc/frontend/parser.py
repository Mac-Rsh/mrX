#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Parser - Recursive descent parser for Hardware Tree Compiler.

Builds syntax nodes from tokens and performs literal decoding that belongs to
the parsing stage rather than the lexer.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..common import (
    ArrayMode,
    ArrayValue,
    Diagnostics,
    EmptyValue,
    ErrorCode,
    FloatValue,
    IdentifierValue,
    NodeDecl,
    NumberValue,
    PropertyDecl,
    ReferenceValue,
    RootNode,
    SourceSpan,
    StringValue,
    ValueExpr,
)
from .tokens import Token, TokenType


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
                 diagnostics: Diagnostics | None = None) -> None:
        """Initialize parser state for one token stream."""
        self.tokens = tokens
        self.diagnostics = diagnostics
        self.pos = 0

    def parse(self) -> RootNode:
        """Parse the full token stream into a root node."""
        root = self._parse_root()
        self._expect(TokenType.EOF, ErrorCode.UNEXPECTED_EOF,
                     "expected end of file")
        return root

    def _parse_root(self) -> RootNode:
        """Parse the top-level `/ { ... }` root declaration."""
        slash = self._expect(TokenType.SLASH, ErrorCode.EXPECTED_SLASH,
                             "expected '/' for root node")
        self._expect(TokenType.LBRACE, ErrorCode.EXPECTED_LBRACE,
                     "expected '{' after '/'")
        children: list[NodeDecl] = []
        while not self._check(TokenType.RBRACE, TokenType.EOF):
            node = self._parse_node()
            if node is not None:
                children.append(node)
        self._expect(TokenType.RBRACE, ErrorCode.EXPECTED_RBRACE,
                     "expected '}' to close root node")
        return RootNode(children=children, span=slash.span if slash else None)

    def _parse_node(self) -> NodeDecl | None:
        """Parse a node declaration including properties and child nodes."""
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

        # Address
        address: str | None = None
        if self._match(TokenType.AT):
            address_token = self._expect(TokenType.NUMBER,
                                         ErrorCode.EXPECTED_NUMBER,
                                         "expected address after '@'")
            if address_token is not None:
                address = address_token.value

        # Expect '{'
        self._expect(TokenType.LBRACE, ErrorCode.EXPECTED_LBRACE,
                     f"expected '{{' after node '{name_token.value}'")

        # Properties and child nodes
        properties: list[PropertyDecl] = []
        children: list[NodeDecl] = []
        while not self._check(TokenType.RBRACE, TokenType.EOF):
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

        # Expect '}'
        self._expect(TokenType.RBRACE, ErrorCode.EXPECTED_RBRACE,
                     f"expected '}}' to close node '{name_token.value}'")
        return NodeDecl(
            name=name_token.value,
            label=label,
            address=address,
            properties=properties,
            children=children,
            span=name_token.span,
        )

    def _parse_property(self) -> PropertyDecl | None:
        """Parse a property assignment or bare property flag."""
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
        return PropertyDecl(name=name_token.value, value=value,
                            span=name_token.span)

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

    def _parse_inline_array(self) -> ArrayValue | None:
        """Parse a bracketed inline array value."""
        # Expect '['
        lbracket = self._expect(TokenType.LBRACKET, ErrorCode.EXPECTED_TOKEN,
                                "expected '[' for inline array")
        if lbracket is None:
            return None

        # Elements
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
