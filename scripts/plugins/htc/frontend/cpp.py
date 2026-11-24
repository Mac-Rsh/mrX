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
            raw_source_text = entry_path.read_text(encoding="utf-8")
        except OSError as exc:
            self.diagnostics.error(
                ErrorCode.FILE_READ_ERROR,
                f"failed to read file '{entry_path}': {exc}",
            )
            return None

        self.diagnostics.register_source(str(entry_path), raw_source_text)
        source_text = self._preprocess_dts_constructs(raw_source_text)

        # Feed transformed source via stdin with an explicit linemarker so we
        # never create temporary files in potentially read-only source dirs.
        escaped_entry = str(entry_path).replace("\\", "\\\\").replace('"', '\\"')
        stdin_payload = f'# 1 "{escaped_entry}"\n{source_text}'
        command = self._build_command(cpp, entry_path.parent, options)
        try:
            completed = subprocess.run(
                command,
                input=stdin_payload,
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

        if completed.returncode != 0:
            self.diagnostics.error(
                ErrorCode.INCLUDE_ERROR,
                "C preprocessor failed",
            ).add_note(completed.stderr.strip() or completed.stdout.strip())
            return None

        text, line_map = self._parse_linemarkers(completed.stdout,
                                                 str(entry_path))
        if not text.strip():
            self.diagnostics.error(
                ErrorCode.INCLUDE_ERROR,
                "C preprocessor produced empty output",
            )
            return None

        # Cache original text of all included files for accurate previews
        for mapped_file, _ in line_map:
            if mapped_file not in self.diagnostics.sources:
                try:
                    self.diagnostics.register_source(
                        mapped_file,
                        Path(mapped_file).read_text(encoding="utf-8"),
                    )
                except OSError:
                    pass

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
    def _build_command(cpp: str, entry_dir: Path,
                       options: CppOptions) -> list[str]:
        """Build the external preprocessor command line reading from stdin."""
        command = [
            cpp,
            "-E",
            "-x",
            "assembler-with-cpp",
            "-undef",
            "-D__DTS__",
            f"-I{entry_dir}",
            "-",
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
                raw_file = CppPreprocessor._unescape_linemarker_path(
                    marker.group(2))
                if raw_file in {"<stdin>", "<built-in>", "<command-line>",
                                "<command line>"}:
                    current_file = fallback_path
                else:
                    current_file = raw_file
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
    def _unescape_linemarker_path(path: str) -> str:
        """Unescape C string escapes inside GNU cpp linemarker filenames."""
        return path.replace('\\"', '"').replace("\\\\", "\\")

    @staticmethod
    def _path_key(path: str) -> str:
        """Return a normalized key for comparing physical source paths."""
        return os.path.normcase(str(Path(path).resolve()))
