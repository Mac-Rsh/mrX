#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
RPC IDL command - Compile `.idl` source files into generated C header and source.

Runs preprocessing, lexing, parsing, semantic extraction, and C emission.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from argparse import ArgumentParser, Namespace
from pathlib import Path
from tempfile import mkstemp
import os

from framework.command import Command, CommandResult, CommandSpec
from framework.completion import DIRECTORY_COMPLETION, FILE_COMPLETION
from framework.completion import PATH_COMPLETION, attach_path_completion
from framework.context import CommandContext
from .compiler import CompileOptions, RpcIdlCompiler
from ..htc.common import ErrorCode


class RpcIdlCommand(Command):
    """Built-in command for compiling RPC `.idl` files."""

    DEFAULT_HEADER_NAME = "mr_rpc_idl.h"
    DEFAULT_SOURCE_NAME = "idl.c"
    DEFAULT_OUTPUT_PATH = f"./include/mrX/modules/rpc/{DEFAULT_HEADER_NAME}"
    DEFAULT_SOURCE_PATH = f"./modules/rpc/{DEFAULT_SOURCE_NAME}"

    spec = CommandSpec(
        name="rpc-idl",
        description=("Compile an RPC `.idl` file into generated C header and "
                     "source"),
    )

    def configure(self, parser: ArgumentParser) -> None:
        """Declare RPC IDL command arguments."""
        attach_path_completion(parser.add_argument(
            "-i",
            "--input",
            required=True,
            help="Input '.idl' file path",
        ), FILE_COMPLETION)
        attach_path_completion(parser.add_argument(
            "-o",
            "--output",
            help=("Output directory or path to "
                  f"'{self.DEFAULT_HEADER_NAME}' "
                  f"(default: {self.DEFAULT_OUTPUT_PATH})"),
        ), PATH_COMPLETION)
        attach_path_completion(parser.add_argument(
            "-c",
            "--source",
            help=(f"Output '.c' file path "
                  f"(default: {self.DEFAULT_SOURCE_PATH})"),
        ), FILE_COMPLETION)
        attach_path_completion(parser.add_argument(
            "-I",
            dest="include_dirs",
            action="append",
            default=[],
            help="Additional include search path (repeatable)",
        ), DIRECTORY_COMPLETION)
        parser.add_argument(
            "--no-cpp",
            action="store_true",
            help=("Use the built-in preprocessor instead of an external "
                  "C preprocessor"),
        )

    def execute(self, context: CommandContext,
                arguments: Namespace) -> CommandResult:
        """Execute RPC IDL header and source generation."""
        paths = self._paths_from_args(context, arguments)
        if isinstance(paths, str):
            return CommandResult.failure(paths)
        input_path, output_path, source_path = paths
        include_dirs = tuple(
            str(context.resolve_path(path))
            for path in (arguments.include_dirs or ())
        )
        code, output = self._compile_rpc_idl(
            input_path,
            output_path,
            source_path,
            include_dirs,
            use_cpp=not arguments.no_cpp,
        )
        if output:
            context.console.write(output)
        return CommandResult(exit_code=code)

    @classmethod
    def _paths_from_args(
            cls,
            context: CommandContext,
            arguments: Namespace,
    ) -> tuple[Path, Path, Path] | str:
        """Resolve input, header, and source paths, or return an error."""
        input_path = context.resolve_path(arguments.input)
        if not input_path.exists():
            return f"Input file not found: {arguments.input}"
        output_path, error = cls._resolve_header_path(
            context, arguments.output)
        if error is not None or output_path is None:
            return error or "invalid -o path"
        if arguments.source is None:
            source_path = context.resolve_path(
                cls.DEFAULT_SOURCE_PATH, project=True)
        else:
            source_path = context.resolve_path(arguments.source)
        if len({input_path, output_path, source_path}) != 3:
            return "RPC IDL input, header, and source paths must be distinct"
        return input_path, output_path, source_path

    @classmethod
    def _resolve_header_path(
            cls,
            context: CommandContext,
            specified: str | None,
    ) -> tuple[Path | None, str | None]:
        """Resolve `-o` while preserving the required header filename."""
        if specified is None:
            return context.resolve_path(
                cls.DEFAULT_OUTPUT_PATH, project=True), None
        path = Path(specified)
        resolved = context.resolve_path(specified)
        as_dir = (specified.endswith(("/", "\\"))
                  or (resolved.exists() and resolved.is_dir())
                  or path.suffix.lower() != ".h")
        if as_dir:
            return (resolved / cls.DEFAULT_HEADER_NAME).resolve(), None
        if path.name != cls.DEFAULT_HEADER_NAME:
            return None, (
                f"-o may only change the output directory; the header must be "
                f"named '{cls.DEFAULT_HEADER_NAME}' (got '{path.name}')"
            )
        return resolved, None

    @classmethod
    def _compile_rpc_idl(
            cls,
            input_path: Path,
            output_path: Path,
            source_path: Path,
            include_dirs: tuple[str, ...],
            use_cpp: bool,
    ) -> tuple[int, str]:
        """Run the RPC IDL pipeline and return an exit code and output."""
        lines = [f"Parsing: {input_path}"]
        result = RpcIdlCompiler().compile(
            CompileOptions(
                input_path,
                output_path=output_path,
                include_dirs=include_dirs,
                use_cpp=use_cpp,
            ))

        if not result.ok:
            if result.diagnostics.diagnostics:
                lines.append(result.diagnostics.format_all())
            return 1, "\n".join(lines)

        error = cls._write_pair(
            output_path,
            result.header_text or "",
            source_path,
            result.source_text or "",
        )
        if error is not None:
            result.diagnostics.error(
                ErrorCode.FILE_WRITE_ERROR,
                "failed to write generated header and source",
            ).add_note(error)
            lines.append(result.diagnostics.format_all())
            return 1, "\n".join(lines)

        if result.diagnostics.diagnostics:
            lines.append(result.diagnostics.format_all())
        lines.append(f"Build: {output_path}")
        lines.append(f"Build: {source_path}")
        return 0, "\n".join(lines)

    @classmethod
    def _write_pair(
            cls,
            header_path: Path,
            header_text: str,
            source_path: Path,
            source_text: str,
    ) -> str | None:
        """Write both outputs atomically and restore the header on failure."""
        header_tmp, error = cls._write_temp(header_path, header_text)
        if error is not None:
            return error
        source_tmp, error = cls._write_temp(source_path, source_text)
        if error is not None:
            cls._unlink(header_tmp)
            return error

        header_bak = None
        header_existed = header_path.exists()
        if header_existed:
            try:
                header_bak, error = cls._write_temp(
                    header_path, header_path.read_text(encoding="utf-8"))
            except OSError as exc:
                error = str(exc)
            if error is not None:
                cls._unlink(header_tmp)
                cls._unlink(source_tmp)
                return error

        try:
            os.replace(header_tmp, header_path)
        except OSError as exc:
            cls._unlink(header_tmp)
            cls._unlink(source_tmp)
            cls._unlink(header_bak)
            return str(exc)

        try:
            os.replace(source_tmp, source_path)
        except OSError as exc:
            rolled = cls._rollback_header(
                header_path, header_bak, header_existed)
            cls._unlink(source_tmp)
            if rolled is not None:
                return f"{exc}; failed to restore '{header_path}': {rolled}"
            return str(exc)

        cls._unlink(header_bak)
        return None

    @classmethod
    def _write_temp(
            cls,
            path: Path,
            text: str,
    ) -> tuple[Path | None, str | None]:
        """Write text beside its destination and return the temporary path."""
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            fd, tmp_name = mkstemp(
                prefix=f".{path.name}.",
                suffix=".tmp",
                dir=str(path.parent),
            )
        except OSError as exc:
            return None, str(exc)
        tmp_path = Path(tmp_name)
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as handle:
                handle.write(text)
        except OSError as exc:
            cls._unlink(tmp_path)
            return None, str(exc)
        return tmp_path, None

    @classmethod
    def _rollback_header(
            cls,
            header_path: Path,
            header_bak: Path | None,
            header_existed: bool,
    ) -> str | None:
        """Undo a header replacement after the source replacement failed."""
        if header_existed and header_bak is not None:
            try:
                os.replace(header_bak, header_path)
            except OSError as exc:
                cls._unlink(header_bak)
                return str(exc)
            return None
        try:
            header_path.unlink()
        except FileNotFoundError:
            pass
        except OSError as exc:
            cls._unlink(header_bak)
            return str(exc)
        cls._unlink(header_bak)
        return None

    @staticmethod
    def _unlink(path: Path | None) -> None:
        """Delete a temporary file, ignoring a missing path."""
        if path is None:
            return
        try:
            path.unlink()
        except OSError:
            pass
