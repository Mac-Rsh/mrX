#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC C Preprocessor Adapter - External C preprocessor integration.

Runs `gcc -E` / `clang -E` / `cpp` on `.hts/.htsi` sources and maps generated
linemarkers back to original source locations when available.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from dataclasses import dataclass
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

from ..common import Diagnostics, ErrorCode, SourceFile


@dataclass(frozen=True, slots=True)
class CppOptions:
    """Options for one external preprocessor invocation."""

    include_dirs: tuple[str, ...] = ()


@dataclass(slots=True)
class PreprocessedUnit:
    """Text and source mapping produced by the preprocessor stage."""

    text: str
    source_file: SourceFile


class CppPreprocessor:
    """Run an external C preprocessor and return preprocessed text."""

    _CPP_CANDIDATES = ("gcc", "clang", "cpp")
    _DTS_V1_PATTERN = re.compile(r"^\s*/\s*dts-v1\s*/\s*;\s*$")
    _LINE_MARKER_PATTERN = re.compile(
        r'^\s*#\s+(\d+)\s+"([^"]+)"(?:\s+(.*))?$')

    def __init__(self, diagnostics: Diagnostics) -> None:
        """Initialize the adapter with a shared diagnostic sink."""
        self.diagnostics = diagnostics

    def preprocess(
            self,
            entry_file: str,
            options: CppOptions | None = None,
    ) -> PreprocessedUnit | None:
        """Preprocess one entry file with an external C preprocessor.

        Arguments:
            entry_file: Resolved input `.hts` or `.htsi` file path.
            options: Optional preprocessor options.

        Returns:
            A `PreprocessedUnit` when preprocessing succeeds, `None` otherwise.
        """
        options = options or CppOptions()
        entry_path = Path(entry_file).resolve()
        if not entry_path.is_file():
            self.diagnostics.error(
                ErrorCode.FILE_NOT_FOUND,
                f"file not found: '{entry_file}'",
            )
            return None

        cpp = self._find_cpp()
        if cpp is None:
            return None

        try:
            source_text = entry_path.read_text(encoding="utf-8")
        except OSError as exc:
            self.diagnostics.error(
                ErrorCode.FILE_READ_ERROR,
                f"failed to read file '{entry_path}': {exc}",
            )
            return None

        source_text = self._preprocess_dts_constructs(source_text)

        # Write the transformed source to a temporary file in the entry
        # directory so cpp linemarkers can be mapped back to the real file.
        fd, tmp_name = tempfile.mkstemp(
            prefix=".htc_", suffix=".hts", dir=entry_path.parent)
        tmp_path = Path(tmp_name)
        os.close(fd)
        try:
            tmp_path.write_text(source_text, encoding="utf-8")
            command = self._build_command(cpp, tmp_path, options)
            try:
                completed = subprocess.run(
                    command,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    encoding="utf-8",
                    errors="replace",
                    check=False,
                )
            except OSError as exc:
                self.diagnostics.error(
                    ErrorCode.INCLUDE_ERROR,
                    f"failed to launch C preprocessor '{cpp}': {exc}",
                )
                return None
        finally:
            tmp_path.unlink(missing_ok=True)

        if completed.returncode != 0:
            self.diagnostics.error(
                ErrorCode.INCLUDE_ERROR,
                "C preprocessor failed",
            ).add_note(completed.stderr.strip() or completed.stdout.strip())
            return None

        text, line_map = self._parse_linemarkers(completed.stdout,
                                                 str(entry_path),
                                                 str(tmp_path))
        if not text.strip():
            self.diagnostics.error(
                ErrorCode.INCLUDE_ERROR,
                "C preprocessor produced empty output",
            )
            return None

        return PreprocessedUnit(
            text=text,
            source_file=SourceFile(path=str(entry_path), text=text,
                                   line_map=line_map),
        )

    def _find_cpp(self) -> str | None:
        """Return the first available C preprocessor binary path."""
        for candidate in self._CPP_CANDIDATES:
            path = shutil.which(candidate)
            if path is not None:
                return path
        return None

    @staticmethod
    def _build_command(cpp: str, input_path: Path,
                       options: CppOptions) -> list[str]:
        """Build the external preprocessor command line."""
        command = [
            cpp,
            "-E",
            "-x",
            "assembler-with-cpp",
            "-undef",
            "-D__DTS__",
            f"-I{input_path.parent}",
            str(input_path),
        ]
        for include_dir in options.include_dirs:
            command.insert(-1, f"-I{include_dir}")
        return command

    @staticmethod
    def _preprocess_dts_constructs(source_text: str) -> str:
        """Translate DTS-only include syntax into C preprocessor includes."""
        lines = []
        for line in source_text.splitlines(keepends=True):
            stripped = line.lstrip(" \t")
            if stripped.startswith("/include/"):
                payload = stripped[len("/include/"):].strip()
                if payload.startswith('"') and payload.endswith('"'):
                    lines.append(f"#include {payload}\n")
                    continue
            lines.append(line)
        return "".join(lines)

    @staticmethod
    def _parse_linemarkers(text: str,
                           fallback_path: str,
                           alias_path: str | None = None) -> tuple[
        str, list[tuple[str, int]]]:
        """Strip GNU cpp linemarkers and build an output line map."""
        clean_lines: list[str] = []
        line_map: list[tuple[str, int]] = []
        current_file = fallback_path
        current_line = 1
        fallback_key = CppPreprocessor._path_key(fallback_path)
        alias_key = CppPreprocessor._path_key(alias_path) \
            if alias_path is not None else None
        seen_paths = {fallback_key}
        # Each frame represents one active CPP include.  The emit flag lets a
        # repeated include suppress its complete expansion, including nested
        # includes, until CPP returns to the including file.
        include_frames: list[bool] = [True]

        for line in text.splitlines():
            marker = CppPreprocessor._LINE_MARKER_PATTERN.match(line)
            if marker is not None:
                current_line = int(marker.group(1))
                current_file = marker.group(2)
                if alias_key is not None \
                        and CppPreprocessor._path_key(
                    current_file) == alias_key:
                    current_file = fallback_path
                flags = set((marker.group(3) or "").split())
                if "1" in flags:
                    path_key = CppPreprocessor._path_key(current_file)
                    emit_file = include_frames[-1] \
                                and path_key not in seen_paths
                    if emit_file:
                        seen_paths.add(path_key)
                    include_frames.append(emit_file)
                elif "2" in flags and len(include_frames) > 1:
                    include_frames.pop()
                continue
            # Included sources may be valid standalone HTC documents and carry
            # their own version tag; CPP flattens those tags into the entry
            # document, where only the entry source tag is meaningful
            is_included_version_tag = \
                CppPreprocessor._DTS_V1_PATTERN.fullmatch(line) is not None \
                and CppPreprocessor._path_key(current_file) != fallback_key
            if include_frames[-1]:
                clean_lines.append("" if is_included_version_tag else line)
                line_map.append((current_file, current_line))
            current_line += 1

        return "\n".join(clean_lines) + "\n", line_map

    @staticmethod
    def _path_key(path: str) -> str:
        """Return a normalized key for comparing physical source paths."""
        return os.path.normcase(str(Path(path).resolve()))
