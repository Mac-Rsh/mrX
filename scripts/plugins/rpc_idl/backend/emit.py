#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IDL Backend Emit - Shared C text helpers for header and source emission.

Holds line layout, section banners, handler prototypes, and small C
declaration helpers used by both builders.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..semantic.model import Method
from datetime import datetime
from .names import CNames


class CEmit:
    """Formats generated C text shared by the header and source builders."""

    INDENT = "    "
    BANNER_WIDTH = 76
    LINE_WIDTH = 80

    @classmethod
    def generated_at(cls) -> str:
        """Return the generated-file timestamp comment."""
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        return f"/* Generated at: {timestamp} */"

    @classmethod
    def banner(cls, title: str) -> str:
        """Build a section banner from a user-facing title."""
        prefix = f"/* ================================ {title} "
        pad = cls.BANNER_WIDTH - len(prefix) - 3
        if pad < 1:
            pad = 1
        return f"{prefix}{'=' * pad} */"

    @classmethod
    def section_banner(cls, kind: str, name: str | None = None) -> str:
        """Build a header section banner with builtin words uppercased."""
        title = kind.upper() if name is None else f"{kind.upper()}/{name}"
        return cls.banner(title)

    @classmethod
    def slash(cls, line: str) -> str:
        """Pad a preprocessor continuation line to the column limit."""
        pad = cls.LINE_WIDTH - 1 - len(line)
        if pad < 1:
            return f"{line} \\"
        return f"{line}{' ' * pad}\\"

    @classmethod
    def handler_decl(cls, service: str, method: Method, indent: str = "",
                     width: int | None = None,
                     named: bool = True) -> list[str]:
        """Format a handler declaration, wrapping parameters when needed."""
        if width is None:
            width = cls.LINE_WIDTH
        name = CNames.handler_fn(service, method.name)
        has_response = (not method.oneway) and bool(method.response)
        params = CNames.handler_params(service, method.name,
                                       bool(method.request), has_response,
                                       named=named)
        if not params:
            return [f"{indent}mr_err_t {name}(void)"]
        joined = f"{indent}mr_err_t {name}({', '.join(params)})"
        if len(joined) <= width:
            return [joined]
        head = f"{indent}mr_err_t {name}("
        pad = " " * len(head)
        lines: list[str] = []
        last = len(params) - 1
        for index, param in enumerate(params):
            prefix = head if index == 0 else pad
            suffix = ")" if index == last else ","
            lines.append(f"{prefix}{param}{suffix}")
        return lines

    @staticmethod
    def arr(name: str, index: int, count: int) -> str:
        """Return `name` or `name[index]` depending on `count`."""
        if count <= 1:
            return name
        return f"{name}[{index}]"

    @staticmethod
    def arr_decl(ctype: str, name: str, count: int) -> str:
        """Declare a scalar or a small array of `count` elements."""
        if count <= 1:
            return f"{ctype} {name};"
        return f"{ctype} {name}[{count}];"
