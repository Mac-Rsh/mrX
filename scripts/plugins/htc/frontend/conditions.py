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
    OP_BITOR = "|"
    OP_BITXOR = "^"
    OP_BITAND = "&"
    OP_EQ = "=="
    OP_NE = "!="
    OP_LE = "<="
    OP_GE = ">="
    OP_LT = "<"
    OP_GT = ">"
    OP_LSHIFT = "<<"
    OP_RSHIFT = ">>"
    OP_ADD = "+"
    OP_SUB = "-"
    OP_MUL = "*"
    OP_DIV = "/"
    OP_MOD = "%"
    OP_NOT = "!"
    OP_BITNOT = "~"
    OP_LPAREN = "("
    OP_RPAREN = ")"
    KEYWORD_DEFINED = "defined"
    DEFAULT_EXPR = "0"

    IDENTIFIER_PATTERN = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')
    INT_SUFFIX_PATTERN = re.compile(r'[uUlL]{1,3}$')
    TOKEN_PATTERN = re.compile(
        r"\s*("
        r"\|\||&&|==|!=|<=|>=|<<|>>|[|&^<>+\-*/%!~()]|defined|"
        r"0[xX][0-9a-fA-F]+[uUlL]{0,3}|"
        r"0[bB][01]+[uUlL]{0,3}|"
        r"0[oO][0-7]+[uUlL]{0,3}|"
        r"\d+[uUlL]{0,3}|"
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
        value = self._parse_bitor()
        while self._match(self.evaluator.OP_AND):
            value = int(bool(value) and bool(self._parse_bitor()))
        return value

    def _parse_bitor(self) -> int:
        """Parse bitwise OR expressions."""
        value = self._parse_bitxor()
        while self._match(self.evaluator.OP_BITOR):
            value = value | self._parse_bitxor()
        return value

    def _parse_bitxor(self) -> int:
        """Parse bitwise XOR expressions."""
        value = self._parse_bitand()
        while self._match(self.evaluator.OP_BITXOR):
            value = value ^ self._parse_bitand()
        return value

    def _parse_bitand(self) -> int:
        """Parse bitwise AND expressions."""
        value = self._parse_equality()
        while self._match(self.evaluator.OP_BITAND):
            value = value & self._parse_equality()
        return value

    def _parse_equality(self) -> int:
        """Parse equality and inequality expressions."""
        value = self._parse_relational()
        while True:
            if self._match(self.evaluator.OP_EQ):
                value = int(value == self._parse_relational())
                continue
            if self._match(self.evaluator.OP_NE):
                value = int(value != self._parse_relational())
                continue
            return value

    def _parse_relational(self) -> int:
        """Parse relational comparison expressions."""
        value = self._parse_shift()
        while True:
            if self._match(self.evaluator.OP_LE):
                value = int(value <= self._parse_shift())
                continue
            if self._match(self.evaluator.OP_GE):
                value = int(value >= self._parse_shift())
                continue
            if self._match(self.evaluator.OP_LT):
                value = int(value < self._parse_shift())
                continue
            if self._match(self.evaluator.OP_GT):
                value = int(value > self._parse_shift())
                continue
            return value

    def _parse_shift(self) -> int:
        """Parse bitwise shift expressions."""
        value = self._parse_additive()
        while True:
            if self._match(self.evaluator.OP_LSHIFT):
                value = value << self._parse_additive()
                continue
            if self._match(self.evaluator.OP_RSHIFT):
                value = value >> self._parse_additive()
                continue
            return value

    def _parse_additive(self) -> int:
        """Parse addition and subtraction expressions."""
        value = self._parse_multiplicative()
        while True:
            if self._match(self.evaluator.OP_ADD):
                value = value + self._parse_multiplicative()
                continue
            if self._match(self.evaluator.OP_SUB):
                value = value - self._parse_multiplicative()
                continue
            return value

    def _parse_multiplicative(self) -> int:
        """Parse multiplication, division, and modulo expressions."""
        value = self._parse_unary()
        while True:
            if self._match(self.evaluator.OP_MUL):
                value = value * self._parse_unary()
                continue
            if self._match(self.evaluator.OP_DIV):
                rhs = self._parse_unary()
                if rhs == 0:
                    raise ValueError("division by zero in condition")
                quotient = abs(value) // abs(rhs)
                value = -quotient if (value < 0) ^ (rhs < 0) else quotient
                continue
            if self._match(self.evaluator.OP_MOD):
                rhs = self._parse_unary()
                if rhs == 0:
                    raise ValueError("modulo by zero in condition")
                value = value % rhs
                continue
            return value

    def _parse_unary(self) -> int:
        """Parse unary logical, bitwise, and sign expressions."""
        if self._match(self.evaluator.OP_NOT):
            return int(not bool(self._parse_unary()))
        if self._match(self.evaluator.OP_BITNOT):
            return ~self._parse_unary()
        if self._match(self.evaluator.OP_ADD):
            return +self._parse_unary()
        if self._match(self.evaluator.OP_SUB):
            return -self._parse_unary()
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
        cleaned = self.evaluator.INT_SUFFIX_PATTERN.sub("", token)
        if (cleaned.startswith("0") and len(cleaned) > 1
                and cleaned[1] not in {"x", "X", "b", "B", "o", "O"}):
            return int(cleaned, 8)
        return int(cleaned, 0)

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
