#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Source Resolver - File and include path resolution helpers.

Resolves source files and include directives for the preprocessor stage.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from pathlib import Path

from ..common import Diagnostics, ErrorCode
from .directives import DirectiveParser


class SourceResolver:
    """Resolves source files and include directives for the preprocessor."""

    INCLUDE_KIND_QUOTE = DirectiveParser.INCLUDE_KIND_QUOTE

    def __init__(self, include_dirs: list[str],
                 diagnostics: Diagnostics) -> None:
        """Create a resolver with extra include search directories."""
        self.include_dirs = [Path(directory).resolve() for directory in
                             include_dirs]
        self.diagnostics = diagnostics

    def require_file(self, file_path: str) -> Path | None:
        """Resolve and validate one source file path."""
        resolved_path = self.resolve_file(Path(file_path))
        if resolved_path is not None:
            return resolved_path
        self.diagnostics.error(ErrorCode.FILE_NOT_FOUND,
                               f"file not found: '{file_path}'")
        return None

    def resolve_include(self, raw_path: str, current_dir: Path,
                        include_kind: str) -> Path | None:
        """Resolve an include path according to quote or angle search rules."""
        candidates: list[Path] = []
        if include_kind == self.INCLUDE_KIND_QUOTE:
            candidates.append(current_dir / raw_path)
        candidates.extend(
            directory / raw_path for directory in self.include_dirs)
        for candidate in candidates:
            resolved = self.resolve_file(candidate)
            if resolved is not None:
                return resolved
        return None

    @staticmethod
    def resolve_file(path: Path) -> Path | None:
        """Resolve a path and ensure it points to a readable file."""
        try:
            resolved = path.resolve()
        except OSError:
            return None
        return resolved if resolved.is_file() else None
