#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IDL Parser - Recursive descent parser for the RPC IDL compiler.

Builds syntax nodes from tokens. Integer decoding belongs to this stage
rather than the lexer. Language-level `include` is not parsed; use
`#include` so the preprocessor inlines files first.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ...htc.common import Diagnostics, ErrorCode, SourceSpan
from .syntax import (
    ArrayType,
    Document,
    EnumDecl,
    EnumItemDecl,
    FieldDecl,
    MethodDecl,
    NamedType,
    PrimType,
    ServiceDecl,
    StructDecl,
    TypeExpr,
    VariableArrayType,
)
from .tokens import Token, TokenType


class Parser:
    """Parses tokens into IDL syntax tree nodes."""

    INT_PRIMS = (
        "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64",
    )
    PRIMS = INT_PRIMS + ("bool", "f32", "f64")
    IDL_KEYWORDS = frozenset((
        "include", "enum", "struct", "service", *PRIMS,
    ))
    C_KEYWORDS = frozenset((
        "auto", "break", "case", "char", "const", "continue", "default",
        "do", "double", "else", "enum", "extern", "float", "for", "goto",
        "if", "inline", "int", "long", "register", "restrict", "return",
        "short", "signed", "sizeof", "static", "struct", "switch",
        "typedef", "union", "unsigned", "void", "volatile", "while",
        "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex",
        "_Generic", "_Imaginary", "_Noreturn", "_Static_assert",
        "_Thread_local",
        "alignas", "alignof", "and", "and_eq", "asm", "bitand", "bitor",
        "bool", "catch", "char8_t", "char16_t", "char32_t", "class",
        "compl", "concept", "const_cast", "consteval", "constexpr",
        "constinit", "co_await", "co_return", "co_yield", "decltype",
        "delete", "dynamic_cast", "explicit", "export", "false",
        "friend", "mutable", "namespace", "new", "noexcept", "not",
        "not_eq", "nullptr", "operator", "or", "or_eq", "private",
        "protected", "public", "reinterpret_cast", "requires",
        "static_assert", "static_cast", "template", "this",
        "thread_local", "throw", "true", "try", "typeid", "typename",
        "using", "virtual", "wchar_t", "xor", "xor_eq",
    ))
    KEYWORDS = IDL_KEYWORDS | C_KEYWORDS
    DECL_KEYWORDS = ("enum", "struct", "service")

    def __init__(self, tokens: list[Token],
                 diagnostics: Diagnostics | None = None) -> None:
        """Initialize parser state for one token stream."""
        self.tokens = tokens
        self.diagnostics = diagnostics
        self.pos = 0

    def parse(self) -> Document:
        """Parse the full token stream into an IDL document."""
        document = Document()

        # Collect top-level declarations until EOF
        while not self._check(TokenType.EOF):
            declaration = self._parse_declaration()
            if declaration is not None:
                document.declarations.append(declaration)
        return document

    def _parse_declaration(self) -> EnumDecl | StructDecl | ServiceDecl | None:
        """Parse one top-level enum, struct, or service declaration."""
        token = self._peek()
        if token is None or token.type != TokenType.IDENTIFIER:
            self._error(ErrorCode.EXPECTED_IDENTIFIER,
                        "expected 'enum', 'struct', or 'service'",
                        token.span if token else None)
            self._sync()
            return None

        # Include is a preprocessor directive, not IDL syntax
        if token.value == "include":
            self._error(
                ErrorCode.INVALID_NODE_DECL,
                "language-level include is not supported; "
                "use #include \"file.idl\"",
                token.span,
            )
            self._sync()
            return None

        # Dispatch on the declaration keyword
        if token.value == "enum":
            return self._parse_enum()
        if token.value == "struct":
            return self._parse_struct()
        if token.value == "service":
            return self._parse_service()

        self._error(ErrorCode.EXPECTED_IDENTIFIER,
                    "expected 'enum', 'struct', or 'service'", token.span)
        self._sync()
        return None

    def _parse_enum(self) -> EnumDecl | None:
        """Parse `enum name: backing { items };`."""
        start = self._peek()
        self._advance()

        # Parse name and backing type
        name = self._expect_name("enum")
        if name is None:
            self._sync()
            return None
        if self._expect(TokenType.COLON, ErrorCode.EXPECTED_TOKEN,
                        "expected ':' after enum name") is None:
            self._sync()
            return None
        backing = self._expect_int_prim()
        if backing is None:
            self._sync()
            return None
        if self._expect(TokenType.LBRACE, ErrorCode.EXPECTED_LBRACE,
                        "expected '{' to start enum body") is None:
            self._sync()
            return None

        # Parse items, allow a trailing comma
        items: list[EnumItemDecl] = []
        if not self._check(TokenType.RBRACE):
            item = self._parse_enum_item()
            if item is None:
                self._sync()
                return None
            items.append(item)
            while self._match(TokenType.COMMA):
                if self._check(TokenType.RBRACE):
                    break
                item = self._parse_enum_item()
                if item is None:
                    self._sync()
                    return None
                items.append(item)

        # Close the enum
        if self._expect(TokenType.RBRACE, ErrorCode.EXPECTED_RBRACE,
                        "expected '}' to close enum body") is None:
            self._sync()
            return None
        if self._expect(TokenType.SEMICOLON, ErrorCode.EXPECTED_SEMICOLON,
                        "expected ';' after enum declaration") is None:
            return None

        # Reject an empty enum
        if not items:
            self._error(ErrorCode.INVALID_NODE_DECL,
                        "enum must contain at least one item",
                        start.span if start else None)
            return None
        return EnumDecl(name, backing, items,
                        start.span if start else None)

    def _parse_enum_item(self) -> EnumItemDecl | None:
        """Parse one `name` or `name = integer` enum item."""
        name_token = self._expect_name_token("enum item")
        if name_token is None:
            return None

        # Optional explicit value
        value = None
        if self._match(TokenType.ASSIGN):
            value = self._expect_int("enum item value")
            if value is None:
                return None
        return EnumItemDecl(name_token.value, value, name_token.span)

    def _parse_struct(self) -> StructDecl | None:
        """Parse `struct name { fields };`."""
        start = self._peek()
        self._advance()
        name = self._expect_name("struct")
        if name is None:
            self._sync()
            return None
        if self._expect(TokenType.LBRACE, ErrorCode.EXPECTED_LBRACE,
                        "expected '{' to start struct body") is None:
            self._sync()
            return None

        # Parse fields until the closing brace
        fields: list[FieldDecl] = []
        while not self._check(TokenType.RBRACE, TokenType.EOF):
            token = self._peek()
            if (token is not None and token.type == TokenType.IDENTIFIER
                    and token.value in self.DECL_KEYWORDS):
                break
            field = self._parse_field()
            if field is None:
                self._sync()
                if self._check(TokenType.RBRACE, TokenType.EOF):
                    break
                continue
            if self._expect(TokenType.SEMICOLON, ErrorCode.EXPECTED_SEMICOLON,
                            "expected ';' after struct field") is None:
                self._sync()
                continue
            fields.append(field)

        # Close the struct
        if self._expect(TokenType.RBRACE, ErrorCode.EXPECTED_RBRACE,
                        "expected '}' to close struct body") is None:
            self._sync()
            return None
        if self._expect(TokenType.SEMICOLON, ErrorCode.EXPECTED_SEMICOLON,
                        "expected ';' after struct declaration") is None:
            return None

        # Reject an empty struct
        if not fields:
            self._error(ErrorCode.INVALID_NODE_DECL,
                        "struct must contain at least one field",
                        start.span if start else None)
            return None
        return StructDecl(name, fields, start.span if start else None)

    def _parse_service(self) -> ServiceDecl | None:
        """Parse `service name[@id] { methods };`."""
        start = self._peek()
        self._advance()
        name = self._expect_name("service")
        if name is None:
            self._sync()
            return None

        # Optional protocol ID
        service_id = self._parse_optional_id()
        if self._expect(TokenType.LBRACE, ErrorCode.EXPECTED_LBRACE,
                        "expected '{' to start service body") is None:
            self._sync()
            return None

        # Parse methods until the closing brace
        methods: list[MethodDecl] = []
        while not self._check(TokenType.RBRACE, TokenType.EOF):
            token = self._peek()
            if (token is not None and token.type == TokenType.IDENTIFIER
                    and token.value in self.DECL_KEYWORDS):
                break
            method = self._parse_method()
            if method is None:
                self._sync()
                if self._check(TokenType.RBRACE, TokenType.EOF):
                    break
                continue
            methods.append(method)

        # Close the service
        if self._expect(TokenType.RBRACE, ErrorCode.EXPECTED_RBRACE,
                        "expected '}' to close service body") is None:
            self._sync()
            return None
        if self._expect(TokenType.SEMICOLON, ErrorCode.EXPECTED_SEMICOLON,
                        "expected ';' after service declaration") is None:
            return None

        # Reject an empty service
        if not methods:
            self._error(ErrorCode.INVALID_NODE_DECL,
                        "service must contain at least one method",
                        start.span if start else None)
            return None
        return ServiceDecl(name, service_id, methods,
                           start.span if start else None)

    def _parse_method(self) -> MethodDecl | None:
        """Parse `name[@id] (fields) [-> (fields)];`."""
        name_token = self._expect_name_token("method")
        if name_token is None:
            return None

        # Optional ID, then request params
        method_id = self._parse_optional_id()
        request = self._parse_params("request")
        if request is None:
            return None

        # Arrow means REQUEST, otherwise ONEWAY
        response = None
        if self._match(TokenType.ARROW):
            response = self._parse_params("response")
            if response is None:
                return None
        if self._expect(TokenType.SEMICOLON, ErrorCode.EXPECTED_SEMICOLON,
                        "expected ';' after method") is None:
            return None
        return MethodDecl(name_token.value, method_id, request, response,
                          name_token.span)

    def _parse_optional_id(self) -> int | None:
        """Parse an optional `@` unsigned ID. Returns None when omitted."""
        if not self._match(TokenType.AT):
            return None
        return self._expect_uint("protocol ID")

    def _parse_params(self, role: str) -> list[FieldDecl] | None:
        """Parse `( [ field { ',' field } ] )`. Trailing commas are errors."""
        if self._expect(TokenType.LPAREN, ErrorCode.EXPECTED_TOKEN,
                        f"expected '(' to start {role} parameters") is None:
            return None

        # Empty `()` is allowed, trailing commas are not
        fields: list[FieldDecl] = []
        if not self._check(TokenType.RPAREN):
            field = self._parse_field()
            if field is None:
                return None
            fields.append(field)
            while self._match(TokenType.COMMA):
                if self._check(TokenType.RPAREN):
                    self._error(ErrorCode.INVALID_PROPERTY,
                                "trailing comma is not allowed in parameters",
                                self._peek().span if self._peek() else None)
                    return None
                field = self._parse_field()
                if field is None:
                    return None
                fields.append(field)
        if self._expect(TokenType.RPAREN, ErrorCode.EXPECTED_TOKEN,
                        f"expected ')' to close {role} parameters") is None:
            return None
        return fields

    def _parse_field(self) -> FieldDecl | None:
        """Parse `name: type`."""
        name_token = self._expect_name_token("field")
        if name_token is None:
            return None
        if self._expect(TokenType.COLON, ErrorCode.EXPECTED_TOKEN,
                        "expected ':' after field name") is None:
            return None
        type_expr = self._parse_type()
        if type_expr is None:
            return None
        return FieldDecl(name_token.value, type_expr, name_token.span)

    def _parse_type(self) -> TypeExpr | None:
        """Parse a type atom followed by fixed or variable array suffixes."""
        atom = self._parse_type_atom()
        if atom is None:
            return None

        # Collect suffixes, then wrap innermost-last so the first dimension
        # stays outermost (`u8[2][3]` -> C `u8[2][3]`).
        suffixes: list[tuple[str, int]] = []
        while True:
            if self._match(TokenType.LBRACKET):
                length = self._expect_uint("array length")
                if length is None:
                    return None
                if self._expect(TokenType.RBRACKET, ErrorCode.EXPECTED_TOKEN,
                                "expected ']' after array length") is None:
                    return None
                if length < 1:
                    self._error(ErrorCode.INVALID_ARRAY,
                                "array length must be >= 1", atom.span)
                    return None
                suffixes.append(("fixed", length))
                continue
            if self._match(TokenType.LT):
                max_count = self._expect_uint("variable array maximum count")
                if max_count is None:
                    return None
                if self._expect(TokenType.GT, ErrorCode.EXPECTED_GT,
                                "expected '>' after variable array count") \
                        is None:
                    return None
                if max_count < 1:
                    self._error(ErrorCode.INVALID_ARRAY,
                                "variable array maximum count must be >= 1",
                                atom.span)
                    return None
                suffixes.append(("variable", max_count))
                continue
            break

        # XLink's MRID reader permits 32 type nodes, including the atom.
        # Check before semantic resolution recurses through the suffix chain.
        if len(suffixes) >= 32:
            self._error(ErrorCode.INVALID_ARRAY,
                        "type exceeds the MRID limit of 31 array dimensions",
                        atom.span)
            return None

        current: TypeExpr = atom
        for kind, count in reversed(suffixes):
            if kind == "fixed":
                current = ArrayType(span=atom.span, element=current,
                                    length=count)
            else:
                current = VariableArrayType(span=atom.span, element=current,
                                            max_count=count)
        return current

    def _parse_type_atom(self) -> TypeExpr | None:
        """Parse a scalar or named type."""
        token = self._peek()
        if token is None or token.type != TokenType.IDENTIFIER:
            self._error(ErrorCode.EXPECTED_IDENTIFIER, "expected a type",
                        token.span if token else None)
            return None
        name = token.value
        span = token.span
        self._advance()

        if name in self.PRIMS:
            return PrimType(span=span, name=name)
        if name in self.KEYWORDS:
            self._error(ErrorCode.INVALID_PROPERTY,
                        f"'{name}' cannot be used as a type name", span)
            return None
        return NamedType(span=span, name=name)

    def _expect_int_prim(self) -> str | None:
        """Require an integer primitive type name."""
        token = self._peek()
        if token is None or token.type != TokenType.IDENTIFIER \
                or token.value not in self.INT_PRIMS:
            self._error(ErrorCode.EXPECTED_IDENTIFIER,
                        "expected an integer primitive type",
                        token.span if token else None)
            return None
        self._advance()
        return token.value

    def _expect_name(self, kind: str) -> str | None:
        """Require a non-keyword identifier and return its text."""
        token = self._expect_name_token(kind)
        return None if token is None else token.value

    def _expect_name_token(self, kind: str) -> Token | None:
        """Require a non-keyword identifier token."""
        token = self._expect(TokenType.IDENTIFIER,
                             ErrorCode.EXPECTED_IDENTIFIER,
                             f"expected a {kind} name")
        if token is None:
            return None

        # Names cannot be IDL, C99, or C++ keywords
        if token.value in self.KEYWORDS:
            self._error(ErrorCode.INVALID_PROPERTY,
                        f"'{token.value}' is a keyword and cannot be a {kind} "
                        "name", token.span)
            return None
        return token

    def _expect_uint(self, role: str) -> int | None:
        """Require an unsigned integer literal."""
        value = self._expect_int(role)
        if value is None:
            return None

        # `@`, `<>`, and `[]` take unsigned values
        if value < 0:
            token = self.tokens[self.pos - 1]
            self._error(ErrorCode.INVALID_NUMBER,
                        f"{role} must be unsigned", token.span)
            return None
        return value

    def _expect_int(self, role: str) -> int | None:
        """Require a decimal or hexadecimal integer literal."""
        token = self._expect(TokenType.NUMBER, ErrorCode.EXPECTED_NUMBER,
                             f"expected an integer for {role}")
        if token is None:
            return None
        return self._decode_int(token)

    def _decode_int(self, token: Token) -> int | None:
        """Decode a NUMBER token into an integer value."""
        raw = token.value
        lowered = raw.lower()

        # Decimal or hexadecimal
        if lowered.startswith("0x") or lowered.startswith("-0x"):
            base = 16
            kind = "hexadecimal"
        else:
            base = 10
            kind = "decimal"
        try:
            return int(raw, base)
        except ValueError:
            self._error(ErrorCode.INVALID_NUMBER,
                        f"invalid {kind} literal", token.span)
            return None

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
        """Recover by skipping to the next declaration boundary."""
        while not self._check(TokenType.SEMICOLON, TokenType.RBRACE,
                              TokenType.EOF):
            token = self._peek()

            # Stop before the next top-level declaration
            if token is not None and token.type == TokenType.IDENTIFIER \
                    and token.value in self.DECL_KEYWORDS and self.pos > 0:
                return
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
