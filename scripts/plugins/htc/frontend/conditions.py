#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Conditions - Conditional expression evaluation helpers.

Tokenizes and evaluates the small expression language used by preprocessor
`#if` and `#elif` directives.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

import re


class ConditionEvaluator:
    """Evaluates preprocessor conditional expressions against macro values."""

    OP_OR = "||"
    OP_AND = "&&"
    OP_EQ = "=="
    OP_NE = "!="
    OP_NOT = "!"
    OP_LPAREN = "("
    OP_RPAREN = ")"
    KEYWORD_DEFINED = "defined"
    DEFAULT_EXPR = "0"

    IDENTIFIER_PATTERN = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')
    TOKEN_PATTERN = re.compile(
        r"\s*("
        r"\|\||&&|==|!=|\(|\)|!|defined|"
        r"0[xX][0-9a-fA-F]+|0[bB][01]+|0[oO][0-7]+|\d+|"
        r"[A-Za-z_][A-Za-z0-9_]*"
        r")"
    )

    def evaluate(self, expression: str, macros: dict[str, str]) -> bool:
        """Evaluate a `#if` or `#elif` expression against current macros."""
        parser = _ConditionParser(
            self.tokenize(expression or self.DEFAULT_EXPR),
            macros, self)
        return bool(parser.parse())

    def tokenize(self, expression: str) -> list[str]:
        """Tokenize a conditional expression into parser-ready pieces."""
        tokens: list[str] = []
        index = 0
        while index < len(expression):
            match = self.TOKEN_PATTERN.match(expression, index)
            if match is None:
                raise ValueError(
                    f"unexpected token near '{expression[index:]}'")
            tokens.append(match.group(1))
            index = match.end()
        return tokens

    def resolve_macro(self, name: str, macros: dict[str, str],
                      seen: set[str] | None = None) -> int:
        """Resolve a macro to an integer value for conditional evaluation."""
        if name not in macros:
            return 0
        seen = set() if seen is None else seen
        if name in seen:
            raise ValueError(f"cyclic macro reference involving '{name}'")
        seen.add(name)
        raw = macros[name].strip()
        if not raw:
            return 1
        if self.IDENTIFIER_PATTERN.fullmatch(raw):
            return self.resolve_macro(raw, macros, seen)
        return _ConditionParser(self.tokenize(raw), macros, self, seen).parse()


class _ConditionParser:
    """Small expression parser used for preprocessor condition evaluation."""

    def __init__(self, tokens: list[str], macros: dict[str, str],
                 evaluator: ConditionEvaluator,
                 seen: set[str] | None = None) -> None:
        """Initialize parser state for one conditional expression."""
        self.tokens = tokens
        self.macros = macros
        self.evaluator = evaluator
        self.seen = set() if seen is None else seen
        self.pos = 0

    def parse(self) -> int:
        """Parse and evaluate the full conditional expression."""
        value = self._parse_or()
        if self._peek() is not None:
            raise ValueError(f"unexpected trailing token '{self._peek()}'")
        return value

    def _parse_or(self) -> int:
        """Parse logical OR expressions."""
        value = self._parse_and()
        while self._match(self.evaluator.OP_OR):
            value = int(bool(value) or bool(self._parse_and()))
        return value

    def _parse_and(self) -> int:
        """Parse logical AND expressions."""
        value = self._parse_equality()
        while self._match(self.evaluator.OP_AND):
            value = int(bool(value) and bool(self._parse_equality()))
        return value

    def _parse_equality(self) -> int | None:
        """Parse equality and inequality expressions."""
        value = self._parse_unary()
        while True:
            if self._match(self.evaluator.OP_EQ):
                value = int(value == self._parse_unary())
                continue
            if self._match(self.evaluator.OP_NE):
                value = int(value != self._parse_unary())
                continue
            return value

    def _parse_unary(self) -> int:
        """Parse unary logical expressions."""
        if self._match(self.evaluator.OP_NOT):
            return int(not bool(self._parse_unary()))
        return self._parse_primary()

    def _parse_primary(self) -> int:
        """Parse primary conditional operands and grouped expressions."""
        token = self._peek()
        if token is None:
            raise ValueError("unexpected end of expression")
        if self._match(self.evaluator.OP_LPAREN):
            value = self._parse_or()
            self._expect(self.evaluator.OP_RPAREN)
            return value
        if token == self.evaluator.KEYWORD_DEFINED:
            self._advance()
            if self._match(self.evaluator.OP_LPAREN):
                name = self._expect_identifier()
                self._expect(self.evaluator.OP_RPAREN)
                return int(name in self.macros)
            return int(self._expect_identifier() in self.macros)
        if self.evaluator.IDENTIFIER_PATTERN.fullmatch(token):
            self._advance()
            return self.evaluator.resolve_macro(token, self.macros,
                                                set(self.seen))
        self._advance()
        return int(token, 0)

    def _expect(self, expected: str) -> None:
        """Require a specific token or raise a parse error."""
        if not self._match(expected):
            raise ValueError(f"expected '{expected}'")

    def _expect_identifier(self) -> str:
        """Require and return an identifier token."""
        token = self._peek()
        if token is None or self.evaluator.IDENTIFIER_PATTERN.fullmatch(
                token) is None:
            raise ValueError("expected identifier")
        self._advance()
        return token

    def _match(self, expected: str) -> bool:
        """Consume the current token when it matches the expected value."""
        if self._peek() != expected:
            return False
        self.pos += 1
        return True

    def _advance(self) -> str:
        """Consume and return the current token string."""
        token = self.tokens[self.pos]
        self.pos += 1
        return token

    def _peek(self) -> str | None:
        """Peek the current token string without consuming it."""
        return None if self.pos >= len(self.tokens) else self.tokens[self.pos]
