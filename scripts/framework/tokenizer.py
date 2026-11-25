#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Command-line tokenizer for interactive input.

Direct execution continues to use `sys.argv`. This tokenizer is only used
by the interactive shell and its completer. It is not a system shell:
comments, pipes, and redirections are ordinary characters.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class Token:
    """One command-line token with its original span."""

    value: str
    start: int
    end: int
    quote: str | None = None


@dataclass(frozen=True, slots=True)
class TokenizeResult:
    """Tokens produced from one command line."""

    tokens: tuple[Token, ...]
    unclosed_quote: str | None = None


@dataclass(frozen=True, slots=True)
class CompletionLine:
    """Split an incomplete line into finished arguments and a prefix."""

    finished: tuple[str, ...]
    prefix: str
    current_start: int
    quote: str | None = None


_WHITESPACE = frozenset(" \t")
_QUOTES = frozenset("\"'")


def tokenize(line: str) -> TokenizeResult:
    """Split a command line while preserving Windows backslashes.

    Double or single quotes group arguments that contain spaces. A doubled
    quote inside a quoted span inserts a literal quote. Backslashes are
    always literal so Windows paths stay intact.

    Arguments:
        line: Raw interactive input.

    Returns:
        Tokens and an unclosed quote marker when the line is incomplete.
    """
    tokens: list[Token] = []
    index = 0
    length = len(line)

    while index < length:
        while index < length and line[index] in _WHITESPACE:
            index += 1
        if index >= length:
            break

        start = index
        chars: list[str] = []
        quote: str | None = None
        used_quote: str | None = None

        while index < length:
            char = line[index]
            if quote is None:
                if char in _WHITESPACE:
                    break
                if char in _QUOTES:
                    quote = char
                    used_quote = char
                    index += 1
                    continue
                chars.append(char)
                index += 1
                continue

            if char == quote:
                if index + 1 < length and line[index + 1] == quote:
                    chars.append(quote)
                    index += 2
                    continue
                quote = None
                index += 1
                continue
            chars.append(char)
            index += 1

        tokens.append(Token(
            value="".join(chars),
            start=start,
            end=index,
            quote=used_quote,
        ))
        if quote is not None:
            return TokenizeResult(tokens=tuple(tokens), unclosed_quote=quote)

    return TokenizeResult(tokens=tuple(tokens))


def split_command_line(line: str) -> list[str]:
    """Split a complete command line into arguments.

    Arguments:
        line: Raw interactive input.

    Returns:
        Argument values ready for `Application.execute`.

    Raises:
        ValueError: The line contains an unclosed quotation mark.
    """
    result = tokenize(line)
    if result.unclosed_quote:
        raise ValueError("Unclosed quotation mark")
    return [token.value for token in result.tokens]


def split_for_completion(line: str) -> CompletionLine:
    """Split an incomplete line for tab completion.

    A trailing unquoted space starts a new empty prefix. Unclosed quotes are
    tolerated so path completion can still run.

    Arguments:
        line: Text before the cursor.

    Returns:
        Finished arguments, the current prefix, and the prefix span.
    """
    result = tokenize(line)
    if not result.tokens:
        return CompletionLine(finished=(), prefix="", current_start=len(line))

    trailing_space = bool(line) and line[-1] in _WHITESPACE and (
            result.unclosed_quote is None)
    if trailing_space:
        return CompletionLine(
            finished=tuple(token.value for token in result.tokens),
            prefix="",
            current_start=len(line),
        )

    current = result.tokens[-1]
    return CompletionLine(
        finished=tuple(token.value for token in result.tokens[:-1]),
        prefix=current.value,
        current_start=current.start,
        quote=current.quote,
    )
