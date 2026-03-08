#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Parser - Syntax analyzer for Hardware Tree Compiler.

Converts token streams into Abstract Syntax Tree (AST).

Include directives are handled by the preprocessor before parsing.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .diagnostics import Diagnostics, ErrorCode
from .lexer import Token, TokenType
from typing import List, Optional
from .ast import (
    ASTNode,
    ArrayValue,
    FloatValue,
    IdentifierValue,
    NumberValue,
    PhandleValue,
    PropertyNode,
    SourceLocation,
    StringValue,
    Value,
)


class Parser:
    """
    Hardware Tree syntax analyzer.

    Responsibilities:
    - Convert token stream into AST (RootNode)
    - Validate syntax structure
    - Provide meaningful error messages with locations

    Grammar:
        Root       ::= SLASH LBRACE (Node)* RBRACE
        Node       ::= (Label COLON)? Identifier (AT NUMBER)? LBRACE
                       (Property)* (Node)* RBRACE
        Property   ::= Identifier EQUAL Value SEMICOLON
        Value      ::= String | Number | Float | Array
        Array      ::= LT Value* GT
        Phandle    ::= AMPERSAND Identifier
    """

    # Constants
    ROOT_NAME = "/"
    MAX_RECURSION_DEPTH = 100

    def __init__(
            self,
            tokens: List[Token],
            diagnostics: Optional[Diagnostics] = None
    ) -> None:
        """
        Initialize the parser.

        Args:
            tokens: Token list from lexer (must include EOF token).
            diagnostics: Optional Diagnostics instance for error reporting.
        """
        self.tokens = tokens
        self.pos = 0
        self.diagnostics = diagnostics
        self._recursion_depth = 0

    def parse(self) -> ASTNode:
        """
        Parse token stream and return ASTNode.

        Returns:
            ASTNode representing the parsed hardware tree (root node).
        """
        root = self._parse_root()

        # Expect EOF
        if not self._match(TokenType.EOF):
            self._emit_error(ErrorCode.UNEXPECTED_EOF, "Expected end of file")

        return root

    def _peek(self, offset: int = 0) -> Optional[Token]:
        """
        Peek at token at specified offset.

        Args:
            offset: Offset from current position.

        Returns:
            Token at offset or None if out of bounds.
        """
        pos = self.pos + offset
        if pos < len(self.tokens):
            return self.tokens[pos]
        return None

    def _advance(self) -> Optional[Token]:
        """
        Advance to next token.

        Returns:
            Current token before advancing, or None if at EOF.
        """
        if self.pos < len(self.tokens):
            token = self.tokens[self.pos]
            self.pos += 1
            return token
        return None

    def _expect(self, token_type: TokenType,
                error_code: ErrorCode = ErrorCode.EXPECTED_TOKEN,
                message: str = "") -> Optional[Token]:
        """
        Expect current token to be of specific type.

        Args:
            token_type: Expected token type.
            error_code: Error code for diagnostics.
            message: Error message if expectation fails.

        Returns:
            Current token if expectation met, None otherwise.
        """
        # Check if current token matches type
        if self._peek() and self._peek().type == token_type:
            return self._advance()

        if not message:
            message = f"Expected {token_type.value}"
        self._emit_error(error_code, message)
        return None

    def _match(self, token_type: TokenType) -> bool:
        """
        Check if current token matches type and consume it.

        Args:
            token_type: Token type to match.

        Returns:
            True if matched and consumed, False otherwise.
        """
        if self._peek() and self._peek().type == token_type:
            self._advance()
            return True
        return False

    def _check(self, *token_types: TokenType) -> bool:
        """
        Check if current token is of the specified types without consuming.

        Args:
            *token_types: Token types to check.

        Returns:
            True if current token matches any of the types.
        """
        if not self._peek():
            return False
        return self._peek().type in token_types

    def _emit_error(self, error_code: ErrorCode, message: str,
                    token: Optional[Token] = None) -> None:
        """
        Report error at specified location.

        Args:
            error_code: Error code for diagnostics.
            message: Error message.
            token: Source token. If None, uses current token.
        """
        if not self.diagnostics:
            return

        # Get token
        if token is None:
            token = self._peek()

        if token:
            self.diagnostics.error(
                error_code,
                message,
            ).with_span(
                token.file_path,
                token.line,
                token.column,
            )
        else:
            self.diagnostics.error(
                error_code,
                message,
            ).with_span("", 0, 0)

    @staticmethod
    def _make_location(token: Token) -> SourceLocation:
        """
        Create SourceLocation from token.

        Args:
            token: Source token.

        Returns:
            SourceLocation instance with file path, line, and column.
        """
        return SourceLocation(
            token.file_path,
            token.line,
            token.column
        )

    def _parse_root(self) -> ASTNode:
        """
        Parse root node: / { ... }.

        Returns:
            ASTNode instance representing root with name="/".
            Note: On parse failure, returns a minimal ASTNode with empty location.
            TODO: Consider returning Optional[ASTNode] for better error handling.
        """
        children: List[ASTNode] = []

        # Root node start
        if not self._expect(TokenType.SLASH,
                            ErrorCode.EXPECTED_SLASH,
                            "Expected '/' for root node"):
            return ASTNode(name=self.ROOT_NAME, children=[],
                           location=SourceLocation("", 0, 0))

        loc = self._make_location(self.tokens[self.pos - 1])

        # Opening brace
        self._expect(TokenType.LBRACE, ErrorCode.EXPECTED_LBRACE,
                     "Expected '{' after '/'")

        # Parse child nodes until closing brace
        while not self._check(TokenType.RBRACE) and not self._check(
                TokenType.EOF):
            child = self._parse_node()
            if child:
                children.append(child)

        # Closing brace
        self._expect(TokenType.RBRACE, ErrorCode.EXPECTED_RBRACE,
                     "Expected '}' to close root node")

        return ASTNode(name=self.ROOT_NAME, children=children, location=loc)

    def _parse_node(self) -> Optional[ASTNode]:
        """
        Parse node declaration: [label:] name[@address] { ... }.

        Returns:
            ASTNode instance or None if parsing failed.
        """
        # Check recursion depth
        self._recursion_depth += 1
        if self._recursion_depth > self.MAX_RECURSION_DEPTH:
            self._recursion_depth -= 1
            self._emit_error(ErrorCode.UNEXPECTED_EOF, "Maximum nesting depth exceeded")
            return None
        
        properties: List[PropertyNode] = []
        address: Optional[str] = None
        children: List[ASTNode] = []
        label_token = None

        # Node start
        peeked = self._peek()
        if not peeked:
            self._emit_error(ErrorCode.UNEXPECTED_EOF,
                             "Unexpected end of input")
            return None
        loc = self._make_location(peeked)

        # Check for label: identifier pattern (look ahead for colon)
        if (self._check(TokenType.IDENTIFIER) and
                self._peek(1) and self._peek(1).type == TokenType.COLON):
            label_token = self._advance()
            self._advance()

        # Parse node name (required)
        name_token = self._expect(TokenType.IDENTIFIER,
                                  ErrorCode.EXPECTED_IDENTIFIER,
                                  "Expected node name")
        if not name_token:
            return None
        name = name_token.value

        # Parse optional @address
        if self._match(TokenType.AT):
            addr_token = self._expect(TokenType.NUMBER,
                                      ErrorCode.EXPECTED_NUMBER,
                                      "Expected address after '@'")
            if addr_token:
                address = addr_token.value

        # Parse opening brace
        self._expect(TokenType.LBRACE, ErrorCode.EXPECTED_LBRACE,
                     f"Expected '{{' after node '{name}'")

        # Search for properties and child nodes
        while not self._check(TokenType.RBRACE) and not self._check(
                TokenType.EOF):
            # Check if this is a property (identifier followed by '=')
            # or a child node (identifier/label at start)
            if self._check(TokenType.IDENTIFIER):
                next_token = self._peek(1)
                if next_token and next_token.type == TokenType.ASSIGN:
                    # This is a property
                    prop = self._parse_property()
                    if prop:
                        properties.append(prop)
                else:
                    # This is a child node
                    child = self._parse_node()
                    if child:
                        children.append(child)
            else:
                self._emit_error(
                    ErrorCode.INVALID_NODE_DECL,
                    f"Expected property or child node in '{name}'")
                # Sync to recovery point
                self._sync()

        # Expect closing brace
        self._expect(TokenType.RBRACE, ErrorCode.EXPECTED_RBRACE,
                     f"Expected '}}' to close node '{name}'")

        self._recursion_depth -= 1
        return ASTNode(
            name=name,
            label=label_token.value if label_token else None,
            address=address,
            properties=properties,
            children=children,
            location=loc,
        )

    def _sync(self) -> None:
        """
        Skip tokens until a synchronization point is reached.

        Sync points are: semicolon, closing brace.
        Used for error recovery during parsing.
        """
        while self._peek():
            if self._check(TokenType.SEMICOLON):
                self._advance()
                return
            if self._check(TokenType.RBRACE):
                self._advance()
                return
            self._advance()

    def _parse_property(self) -> Optional[PropertyNode]:
        """
        Parse property: name = value; or name; (empty property).

        Returns:
            PropertyNode instance or None if parsing failed.
        """
        # Parse name
        name_token = self._expect(TokenType.IDENTIFIER,
                                  ErrorCode.EXPECTED_IDENTIFIER,
                                  "Expected property name")
        if not name_token:
            return None
        name = name_token.value

        # Check if this is an empty property (no assignment)
        if not self._check(TokenType.ASSIGN):
            # Empty property - expect semicolon directly
            self._expect(TokenType.SEMICOLON, ErrorCode.EXPECTED_SEMICOLON,
                         f"Expected ';' after empty property '{name}'")
            return PropertyNode(name=name, value=StringValue(""),
                                location=self._make_location(name_token))

        # Expect assignment
        self._expect(TokenType.ASSIGN, ErrorCode.EXPECTED_ASSIGN,
                     f"Expected '=' after property '{name}'")

        # Parse value
        value = self._parse_value()

        # Expect semicolon
        self._expect(TokenType.SEMICOLON, ErrorCode.EXPECTED_SEMICOLON,
                     f"Expected ';' after value for '{name}'")

        # Empty value is not allowed
        if value is None:
            return None

        return PropertyNode(name=name, value=value,
                            location=self._make_location(name_token))

    @staticmethod
    def _make_number(token: Token) -> NumberValue:
        """
        Convert number token to NumberValue.

        Handles both decimal (e.g., "42") and hexadecimal (e.g., "0x2A") formats.

        Args:
            token: Number token from lexer.

        Returns:
            NumberValue with parsed integer value.
        """
        if token.value.startswith("0x") or token.value.startswith("0X"):
            return NumberValue(value=int(token.value, 16), original=token.value)
        return NumberValue(value=int(token.value), original=token.value)

    @staticmethod
    def _make_float(token: Token) -> FloatValue:
        """
        Convert float token to FloatValue.

        Args:
            token: Float token from lexer.

        Returns:
            FloatValue with parsed float value.
        """
        return FloatValue(value=float(token.value), original=token.value)

    def _parse_phandle(self) -> Optional[PhandleValue]:
        """
        Parse phandle reference: &label or &{/path/to/node}.

        Returns:
            PhandleValue instance or None if parsing failed.
        """
        if not self._peek():
            return None

        # Consume &
        self._advance()

        # Check if this is a path reference: &{/path}
        # Token sequence: AMPERSAND LBRACE SLASH ...
        if (self._check(TokenType.LBRACE) and
                self._peek(1) and self._peek(1).type == TokenType.SLASH):
            # Path reference: &{/path/to/node}
            return self._parse_path_phandle()

        # Label reference: &label
        ref_token = self._expect(TokenType.IDENTIFIER,
                                 ErrorCode.EXPECTED_IDENTIFIER,
                                 "Expected identifier or '{' after '&'")
        if ref_token:
            return PhandleValue(reference=ref_token.value, is_path=False)
        return None

    def _parse_path_phandle(self) -> Optional[PhandleValue]:
        """
        Parse path reference: &{/path/to/node}.

        Token sequence: AMPERSAND LBRACE (already consumed) SLASH IDENTIFIER SLASH ... RBRACE

        Returns:
            PhandleValue with is_path=True, or None if parsing failed.
        """
        # Consume { / (the & was already consumed, now at {)
        if not self._expect(TokenType.LBRACE, ErrorCode.EXPECTED_LBRACE,
                            "Expected '{' after '&' for path reference"):
            return None
        if not self._expect(TokenType.SLASH, ErrorCode.EXPECTED_SLASH,
                            "Expected '/' after '{' for path reference"):
            return None

        # Build path: /path/to/node
        path_parts = ["/"]

        while not self._check(TokenType.RBRACE) and not self._check(
                TokenType.EOF):
            if self._check(TokenType.SLASH):
                path_parts.append("/")
                self._advance()
            elif self._check(TokenType.IDENTIFIER):
                path_parts.append(self._advance().value)
            else:
                # Unknown token in path - report error and skip
                self._emit_error(ErrorCode.EXPECTED_IDENTIFIER,
                                 f"Expected identifier or '/' in path reference, found '{self._peek().type.value}'")
                self._advance()

        # Expect closing }
        if not self._expect(TokenType.RBRACE, ErrorCode.EXPECTED_RBRACE,
                            "Expected '}' to close path reference"):
            return None

        return PhandleValue(reference="".join(path_parts), is_path=True)

    def _parse_value(self) -> Optional[Value]:
        """
        Parse property value.

        Value syntaxes:
        - Single value: "string", 123, identifier, <&label 0>
        - Comma-separated arrays: <"a" "b">, <"c" "d">
        - Space-separated strings: "a" "b" "c" (treated as array)

        Returns:
            Value instance or None if parsing failed.
        """
        # Parse first value
        first = self._parse_value_atom()
        if first is None:
            return None

        # Check if there's a comma - if so, parse comma-separated arrays
        if self._match(TokenType.COMMA):
            values = [first]
            while True:
                next_val = self._parse_value_atom()
                if next_val is None:
                    # Error already reported, sync to delimiter
                    self._sync_to_delim()
                    return None
                values.append(next_val)

                # Continue if there's another comma
                if not self._match(TokenType.COMMA):
                    break

            return ArrayValue(elements=values)

        # Check for space-separated values (without commas)
        # This handles cases like: array = "a" "b" 123;
        # Look ahead: if we have STRING/NUMBER/FLOAT/IDENTIFIER without comma, treat as array
        if self._check(TokenType.STRING, TokenType.NUMBER, TokenType.FLOAT,
                       TokenType.IDENTIFIER):
            values = [first]
            while self._check(TokenType.STRING, TokenType.NUMBER,
                              TokenType.FLOAT, TokenType.IDENTIFIER):
                next_val = self._parse_value_atom()
                if next_val is None:
                    break
                values.append(next_val)

            # If we collected multiple values, return as array
            if len(values) > 1:
                return ArrayValue(elements=values)

        return first

    def _sync_to_delim(self) -> None:
        """
        Skip tokens until a synchronization point is reached.

        Sync points are: semicolon, closing brace.
        Used for error recovery during value parsing.
        """
        while self._peek():
            if self._check(TokenType.SEMICOLON) or self._check(
                    TokenType.RBRACE):
                return
            self._advance()

    def _parse_value_atom(self) -> Optional[Value]:
        """
        Parse a single value (string, number, float, array, phandle, or identifier).

        Returns:
            Value instance or None if parsing failed.
        """
        token = self._peek()
        if not token:
            self._emit_error(ErrorCode.EXPECTED_TOKEN, "Expected value")
            return None

        # Simple value types - consume token and return value
        if token.type == TokenType.STRING:
            self._advance()
            return StringValue(value=token.value)
        if token.type == TokenType.NUMBER:
            self._advance()
            return self._make_number(token)
        if token.type == TokenType.FLOAT:
            self._advance()
            return self._make_float(token)
        if token.type == TokenType.IDENTIFIER:
            self._advance()
            return IdentifierValue(name=token.value)

        # Complex value types - handler consumes its own tokens
        if token.type == TokenType.LT:
            return self._parse_array()
        if token.type == TokenType.AMPERSAND:
            return self._parse_phandle()

        # Unknown token type - report error and advance
        self._emit_error(ErrorCode.EXPECTED_TOKEN,
                         f"Expected value, found '{token.type.value}'")
        self._advance()
        return None

    def _parse_array(self) -> Optional[ArrayValue]:
        """
        Parse array value: <elem1 elem2 ... >.

        Elements inside arrays can be:
        - String literals: "hello"
        - Numbers: 123, 0x1A
        - Floats: 3.14
        - Phandles: &label, &{/path}
        - Identifiers: MR_I2C_OPS_MODE_POLL

        Returns:
            ArrayValue instance or None if parsing failed.
        """
        elements: List[Value] = []

        # Expect opening <
        if not self._expect(TokenType.LT, ErrorCode.EXPECTED_LT,
                            "Expected '<' for array"):
            return None

        # Parse elements until closing >
        while not self._check(TokenType.GT) and not self._check(TokenType.EOF):
            # Skip trailing commas inside arrays
            if self._check(TokenType.COMMA):
                self._advance()
                continue

            value = self._parse_value_atom()
            if value:
                elements.append(value)
            else:
                # Error recovery: skip to GT, comma, or EOF
                while self._peek():
                    if self._check(TokenType.GT, TokenType.COMMA,
                                   TokenType.EOF):
                        break
                    self._advance()
                # If we found GT, break out of the main loop
                if self._check(TokenType.GT):
                    break

        # Expect closing >
        self._expect(TokenType.GT, ErrorCode.EXPECTED_GT,
                     "Expected '>' to close array")

        return ArrayValue(elements=elements)

    @staticmethod
    def merge(asts: List[ASTNode]) -> ASTNode | None:
        """Merge multiple root AST nodes into one.

        This is useful when preprocessor returns multiple files
        (e.g., from include directives) and you want to combine
        them into a single AST for semantic analysis.

        Args:
            asts: List of root AST nodes to merge.

        Returns:
            Merged root AST node with all children combined.
        """
        if not asts:
            return None
        if len(asts) == 1:
            return asts[0]

        main_root = asts[0]
        for root in asts[1:]:
            main_root.children.extend(root.children)
        return main_root
