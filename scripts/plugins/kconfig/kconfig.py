#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Kconfig command - Process Kconfig files and generate a C header.

Loads a `Kconfig` tree and `.config` file, optionally launches `menuconfig`,
then writes normalized configuration output and an exported header file.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from argparse import ArgumentParser, Namespace
from collections.abc import Mapping
from datetime import datetime
from pathlib import Path
import subprocess

import kconfiglib

from framework.command import Command, CommandResult, CommandSpec
from framework.completion import FILE_COMPLETION, attach_path_completion
from framework.context import CommandContext


class KconfigCommand(Command):
    """Built-in command for processing Kconfig configuration files."""

    DEFAULT_KCONFIG_PATH = "Kconfig"
    DEFAULT_CONFIG_PATH = ".config"
    DEFAULT_OUTPUT_PATH = "./include/mrX/mr_config.h"

    spec = CommandSpec(
        name="kconfig",
        description="Process Kconfig and generate a C header",
        version="1.0.0",
    )

    def configure(self, parser: ArgumentParser) -> None:
        """Declare Kconfig command arguments."""
        attach_path_completion(parser.add_argument(
            "-k",
            "--kconfig",
            default=None,
            help=f"Kconfig file path (default: {self.DEFAULT_KCONFIG_PATH})",
        ), FILE_COMPLETION)
        attach_path_completion(parser.add_argument(
            "-c",
            "--config",
            default=None,
            help=f".config file path (default: {self.DEFAULT_CONFIG_PATH})",
        ), FILE_COMPLETION)
        attach_path_completion(parser.add_argument(
            "-o",
            "--output",
            default=None,
            help=("Output header file path "
                  f"(default: {self.DEFAULT_OUTPUT_PATH})"),
        ), FILE_COMPLETION)
        parser.add_argument(
            "--no-ui",
            action="store_true",
            help="Disable menuconfig UI",
        )

    def execute(self, context: CommandContext,
                arguments: Namespace) -> CommandResult:
        """Execute the Kconfig processing workflow."""
        kconfig_path = context.resolve_optional_path(
            arguments.kconfig, self.DEFAULT_KCONFIG_PATH)
        if not kconfig_path.exists():
            return CommandResult.failure(
                "Kconfig file not found: "
                f"{arguments.kconfig or self.DEFAULT_KCONFIG_PATH}"
            )

        config_path = context.resolve_optional_path(
            arguments.config, self.DEFAULT_CONFIG_PATH)
        output_path = context.resolve_optional_path(
            arguments.output, self.DEFAULT_OUTPUT_PATH)

        if not arguments.no_ui:
            try:
                self._show_ui(
                    context.project_root,
                    kconfig_path,
                    config_path,
                    context.environment,
                )
            except RuntimeError as exc:
                return CommandResult.failure(str(exc))

        if not config_path.exists():
            return CommandResult.failure(
                "Config file not found: "
                f"{arguments.config or self.DEFAULT_CONFIG_PATH}"
            )

        error = self._export_config(kconfig_path, config_path, output_path)
        if error is not None:
            return CommandResult.failure(f"Failed to export Kconfig: {error}")

        header_text = self._wrap_output(output_path)
        if not self._write_output(output_path, header_text):
            return CommandResult.failure("Failed to rewrite header file")

        context.console.write(f"Build: {output_path}")
        return CommandResult.success()

    @staticmethod
    def _show_ui(
            project_root: Path,
            kconfig_path: Path,
            config_path: Path,
            environment: Mapping[str, str],
    ) -> None:
        """Show the menuconfig user interface.

        Raises:
            RuntimeError: `menuconfig` failed to launch successfully.
        """
        process_environment = dict(environment)
        process_environment["KCONFIG_CONFIG"] = str(config_path)
        try:
            subprocess.run(
                ["menuconfig", str(kconfig_path)],
                check=True,
                cwd=str(project_root),
                env=process_environment,
            )
        except FileNotFoundError as exc:
            raise RuntimeError(
                "Showing menuconfig failed. Please make sure menuconfig "
                "is installed."
            ) from exc
        except subprocess.CalledProcessError as exc:
            raise RuntimeError(f"Showing menuconfig failed: {exc}") from exc

    @staticmethod
    def _export_config(
            kconfig_path: Path,
            config_path: Path,
            output_path: Path,
    ) -> str | None:
        """Load Kconfig, write `.config`, and export the autoconf header.

        Arguments:
            kconfig_path: Resolved `Kconfig` file path.
            config_path: Resolved `.config` file path.
            output_path: Resolved generated header output path.

        Returns:
            `None` on success, otherwise an error message.
        """
        try:
            output_path.parent.mkdir(parents=True, exist_ok=True)
            kconf = kconfiglib.Kconfig(
                str(kconfig_path),
                warn=False,
                warn_to_stderr=False,
            )
            kconf.load_config(str(config_path))
            kconf.write_config(str(config_path))
            kconf.write_autoconf(str(output_path))
        except Exception as exc:
            return str(exc)
        return None

    @classmethod
    def _wrap_output(cls, output_path: Path) -> str:
        """Wrap generated autoconf output in the project header template.

        Arguments:
            output_path: Resolved generated header file path.

        Returns:
            Header text ready to replace the autoconf file.
        """
        content = output_path.read_text(encoding="utf-8")
        content = content.replace("#define CONFIG_", "#define ")
        guard = cls._build_header_guard(output_path)
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        lines = [
            f"#ifndef {guard}",
            f"#define {guard}",
            "",
            f"/* Generated at: {timestamp} */",
            "",
            "#ifdef __cplusplus",
            'extern "C" {',
            "#endif /* __cplusplus */",
            "",
        ]
        body = content.strip()
        if body:
            lines.extend([body, ""])
        lines.extend([
            "#ifdef __cplusplus",
            "}",
            "#endif /* __cplusplus */",
            "",
            f"#endif /* {guard} */",
            "",
        ])
        return "\n".join(lines)

    @staticmethod
    def _build_header_guard(output_path: Path) -> str:
        """Build an include guard macro name from the output file name.

        Arguments:
            output_path: Resolved output header file path.

        Returns:
            Include guard macro derived from the output file name.
        """
        name = output_path.name.upper()
        guard = []
        for char in name:
            guard.append(char if char.isalnum() else "_")
        return f"__{''.join(guard)}__"

    @staticmethod
    def _write_output(
            output_path: Path,
            header_text: str,
    ) -> bool:
        """Write compiled header text to the requested output path.

        Arguments:
            output_path: Resolved output header file path.
            header_text: Rendered header text.

        Returns:
            `True` when the output file is written successfully, else `False`.
        """
        try:
            output_path.parent.mkdir(parents=True, exist_ok=True)
            output_path.write_text(header_text, encoding="utf-8")
        except OSError:
            return False

        return True
