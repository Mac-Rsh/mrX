#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Preprocessor - Text-level preprocessing for include directives.

Responsibilities:
- Process include directives at text level (before lexing)
- Resolve include file paths
- Detect circular includes
- Remove include directives from file content

The preprocessor runs BEFORE lexical analysis, handling /include/"path"
directives by reading and collecting files.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .diagnostics import Diagnostics, ErrorCode
from dataclasses import dataclass
from typing import Final, Set
from pathlib import Path
import re


@dataclass
class FileInfo:
    """Represents a single file in the compilation unit."""
    file_path: str
    content: str
    included_from: tuple[str, int] | None = None


# Alias for backward compatibility with tests
IncludeResult = FileInfo


class Preprocessor:
    """
    Text-level preprocessor for HTS files.

    Processes include directives by:
    1. Reading the main file
    2. Scanning for /include/"path" directives
    3. Resolving file paths (relative, include dirs, absolute)
    4. Recursively processing included files
    5. Returning file list with include directives removed
    """

    INCLUDE_PATTERN: Final[re.Pattern[str]] = re.compile(r'^/include/"([^"]+)"')
    """Regex pattern to match include directives."""

    def __init__(
            self,
            include_dirs: list[str],
            diagnostics: Diagnostics,
    ) -> None:
        """
        Initialize preprocessor.

        Args:
            include_dirs: List of directories to search for included files.
            diagnostics: Diagnostics instance for error reporting.
        """
        self.include_dirs = [Path(d) for d in include_dirs]
        self.diagnostics = diagnostics

    def process(self, main_file: str) -> list[FileInfo]:
        """
        Process main file and all includes.

        Args:
            main_file: Path to the main input file.

        Returns:
            Ordered list of FileInfo (root first).
        """
        files: list[FileInfo] = []
        self._scan_file_recursive(main_file, files, included=set(),
                                  is_root=True)
        return files

    def _scan_file_recursive(
            self,
            file_path: str,
            files: list[FileInfo],
            included: Set[str],
            is_root: bool = True,
            parent_file: str | None = None,
            parent_line: int | None = None,
    ) -> None:
        """
        Scan a file and recursively scan its includes.

        Args:
            file_path: Path to the file to scan.
            files: Output list of FileInfo objects.
            included: Set of already included file paths (for circular detection).
            is_root: True if this is the root file.
            parent_file: Parent file path if included from another file.
            parent_line: Line number in parent file where include directive appears.
        """
        # Resolve to real path for circular detection
        try:
            real_path = str(Path(file_path).resolve())
        except OSError:
            self.diagnostics.error(
                ErrorCode.FILE_NOT_FOUND,
                f"file not found: '{file_path}'",
            )
            return

        # Add to included set first (for both root and non-root)
        included.add(real_path)

        # Check for circular include (only for non-root nodes)
        if not is_root and len(included) > 1:
            # Count occurrences of this path
            if list(included).count(real_path) > 1:
                self.diagnostics.error(
                    ErrorCode.FILE_READ_ERROR,
                    f"circular include detected: '{file_path}'",
                )
                included.remove(real_path)
                return

        # Read file content and filter out include directives
        try:
            raw_content = Path(file_path).read_text(encoding="utf-8")
            lines = raw_content.splitlines(keepends=True)
            filtered_lines = [
                line for line in lines
                if not self.INCLUDE_PATTERN.match(line.strip())
            ]
            content = ''.join(filtered_lines)
        except IOError as e:
            self.diagnostics.error(
                ErrorCode.FILE_READ_ERROR,
                f"failed to read file '{file_path}': {e}",
            )
            return

        # Add to files list (include directives removed)
        abs_path = str(Path(file_path).resolve())
        included_from = (
            (parent_file, parent_line)
            if parent_file is not None and parent_line is not None
            else None
        )
        files.append(FileInfo(
            file_path=abs_path,
            content=content,
            included_from=included_from,
        ))

        # Get directory of current file for relative includes
        current_dir = Path(file_path).parent

        # Scan lines for include directives (use raw content for scanning)
        for line_num, line in enumerate(lines, 1):
            stripped = line.strip()

            # Check for include directive
            match = self.INCLUDE_PATTERN.match(stripped)
            if match:
                include_path = match.group(1)
                resolved_path = self._resolve_path(
                    include_path, current_dir
                )

                if resolved_path:
                    # Recursively scan included file
                    self._scan_file_recursive(
                        resolved_path,
                        files,
                        included,
                        is_root=False,
                        parent_file=abs_path,
                        parent_line=line_num,
                    )
                else:
                    self.diagnostics.error(
                        ErrorCode.FILE_NOT_FOUND,
                        f"include file not found: '{include_path}'",
                    ).with_span(file_path, line_num, 1)

        # Remove from included set when returning (allows same file in different branches)
        if not is_root:
            included.remove(real_path)

    def _resolve_path(self, path: str, current_dir: Path) -> str | None:
        """
        Resolve include file path.

        Search order:
        1. Relative to current file
        2. Include directories
        3. Absolute path or relative to cwd

        Args:
            path: Include path from directive.
            current_dir: Directory of current file.

        Returns:
            Resolved absolute path if found, None otherwise.
        """
        # Security check: reject path traversal attempts
        if ".." in path or path.startswith("/"):
            self.diagnostics.error(
                ErrorCode.INCLUDE_ERROR,
                f"path traversal detected in include: '{path}'",
            )
            return None

        # Try relative to current file
        relative_path = current_dir / path
        if relative_path.is_file():
            resolved = relative_path.resolve()
            # Additional security: check resolved path doesn't escape include dirs
            for include_dir in self.include_dirs:
                try:
                    resolved.relative_to(include_dir.resolve())
                    return str(resolved)
                except ValueError:
                    pass
            # Also allow relative to current file's directory
            try:
                resolved.relative_to(current_dir.resolve())
                return str(resolved)
            except ValueError:
                pass

        # Try include directories
        for include_dir in self.include_dirs:
            include_path = include_dir / path
            if include_path.is_file():
                resolved = include_path.resolve()
                # Verify the resolved path is within the include directory
                try:
                    resolved.relative_to(include_dir.resolve())
                    return str(resolved)
                except ValueError:
                    self.diagnostics.error(
                        ErrorCode.INCLUDE_ERROR,
                        f"include path resolves outside include directory: '{path}'",
                    )
                    return None

        # Try as relative to cwd (for convenience, but warn)
        abs_path = Path(path)
        if abs_path.is_file():
            return str(abs_path.resolve())

        return None
