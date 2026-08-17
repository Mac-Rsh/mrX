#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Backend Names - Macro-safe name normalization rules.

Provides one canonical mapping from DTS node paths and property names to C
preprocessor macro fragments.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""


class MacroNames:
    """Normalizes DTS names into macro-safe fragments."""

    @staticmethod
    def normalize_identifier(identifier: str) -> str:
        """Normalize a user-facing identifier into a macro-safe fragment."""
        result = []
        for char in identifier:
            if char.isalnum():
                result.append(char)
            else:
                result.append("_")
        normalized = "".join(result)
        if normalized and normalized[0].isdigit():
            return f"_{normalized}"
        return normalized

    @classmethod
    def normalize_path(cls, path: str) -> str:
        """Normalize a canonical node path into a macro-safe path fragment."""
        parts = path.split("/")
        normalized_parts = [cls.normalize_identifier(part) for part in parts
                            if part]
        return "_S_".join(normalized_parts)

    @classmethod
    def node_macro(cls, path: str) -> str:
        """Build the full node macro name for a canonical DTS path."""
        if path == "/":
            return "__MR_HT_N"
        return f"__MR_HT_N_S_{cls.normalize_path(path)}"
