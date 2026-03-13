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
from dock import PluginSpec, export_plugin
from datetime import datetime
from pathlib import Path
import subprocess
import kconfiglib


@export_plugin
class KconfigCommand:
    """Built-in command for processing Kconfig configuration files."""

    DEFAULT_KCONFIG_PATH = "Kconfig"
    DEFAULT_CONFIG_PATH = ".config"
    DEFAULT_OUTPUT_PATH = "./include/mrX/mr_config.h"

    spec = PluginSpec(
        name="kconfig",
        description="Process Kconfig and generate a C header.",
        version="1.0.0",
        author="MacRsh",
    )

    def help(self) -> str:
        """Return detailed help text for the Kconfig command.

        Returns:
            Human-readable help text for the command.
        """
        parser = self._build_parser()
        lines = [
            "Usage: python ./scripts/cli.py kconfig [options]",
            "Description: Process Kconfig and generate a C header.",
            "Aliases: (none)",
            "",
            parser.format_help().rstrip(),
            f"Default Kconfig: {self.DEFAULT_KCONFIG_PATH}",
            f"Default config: {self.DEFAULT_CONFIG_PATH}",
            f"Default output: {self.DEFAULT_OUTPUT_PATH}",
        ]
        return "\n".join(lines)

    def run(self, args: list[str]) -> int:
        """Execute the Kconfig processing workflow.

        Args:
            args: Command line arguments passed to the command.

        Returns:
            Exit code (0 for success, non-zero for error).
        """
        # Parse command line arguments
        options = self._parse_args(args)
        if options is None:
            return 1

        # Parse and validate input paths
        paths = self._parse_paths(options)
        if paths is None:
            return 1
        kconfig_path, config_path, output_path = paths

        # Show menuconfig UI before loading configuration when requested
        if not options.no_ui:
            self._show_ui()

        # Load and export Kconfig data
        if self._parse_kconfig(kconfig_path, config_path, output_path) is None:
            return 1

        # Rewrite generated header
        header_text = self._wrap_output(output_path)

        # Write output file
        if not self._write_output(output_path, header_text):
            print("Failed to rewrite header file.")
            return 1

        # Report successful completion
        print(f"Build: {output_path}")
        return 0

    @classmethod
    def _build_parser(cls) -> ArgumentParser:
        """Build the Kconfig command line argument parser.

        Returns:
            Configured argument parser for the Kconfig command.
        """
        parser = ArgumentParser(
            prog="python ./scripts/cli.py kconfig",
            add_help=True,
        )
        parser.add_argument(
            "-k",
            "--kconfig",
            default=cls.DEFAULT_KCONFIG_PATH,
            help=f"Kconfig file path (default: {cls.DEFAULT_KCONFIG_PATH})",
        )
        parser.add_argument(
            "-c",
            "--config",
            default=cls.DEFAULT_CONFIG_PATH,
            help=f".config file path (default: {cls.DEFAULT_CONFIG_PATH})",
        )
        parser.add_argument(
            "-o",
            "--output",
            default=cls.DEFAULT_OUTPUT_PATH,
            help=f"Output header file path (default: {cls.DEFAULT_OUTPUT_PATH})",
        )
        parser.add_argument(
            "--no-ui",
            action="store_true",
            help="Disable menuconfig UI.",
        )
        return parser

    @classmethod
    def _parse_args(cls, args: list[str]) -> Namespace | None:
        """Parse Kconfig command line arguments.

        Args:
            args: Raw command line arguments passed to the command.

        Returns:
            Parsed argument namespace, or `None` when parsing fails.
        """
        parser = cls._build_parser()
        try:
            return parser.parse_args(args)
        except SystemExit:
            return None

    @staticmethod
    def _parse_paths(args: Namespace) -> tuple[Path, Path, Path] | None:
        """Parse and validate command input and output paths.

        Args:
            args: Parsed Kconfig command line arguments.

        Returns:
            Resolved Kconfig, config, and output paths, or `None` on error.
        """
        # Kconfig file path
        kconfig_path = Path(args.kconfig).resolve()
        if not kconfig_path.exists():
            print(f"Kconfig file not found: {args.kconfig}")
            return None

        # Config file path
        config_path = Path(args.config).resolve()
        if not config_path.exists():
            print(f"Config file not found: {args.config}")
            return None

        # Output file path
        output_path = Path(args.output).resolve()

        return kconfig_path, config_path, output_path

    @staticmethod
    def _show_ui() -> None:
        """Show the menuconfig user interface.

        Raises:
            RuntimeError: `menuconfig` failed to launch successfully.
        """
        try:
            subprocess.run(["menuconfig"], check=True)
        except FileNotFoundError as exc:
            raise RuntimeError(
                "Showing menuconfig failed. Please make sure menuconfig is installed."
            ) from exc
        except subprocess.CalledProcessError as exc:
            raise RuntimeError(f"Showing menuconfig failed: {exc}") from exc

    @staticmethod
    def _parse_kconfig(
            kconfig_path: Path,
            config_path: Path,
            output_path: Path,
    ) -> kconfiglib.Kconfig | None:
        """Parse the Kconfig tree and export autoconfiguration output.

        Args:
            kconfig_path: Resolved `Kconfig` file path.
            config_path: Resolved `.config` file path.
            output_path: Resolved generated header output path.

        Returns:
            Loaded `kconfiglib.Kconfig` instance, or `None` on failure.
        """
        try:
            # Ensure output parent directory exists before writing
            output_path.parent.mkdir(parents=True, exist_ok=True)

            # Load Kconfig tree and `.config` contents
            kconf = kconfiglib.Kconfig(
                str(kconfig_path),
                warn=False,
                warn_to_stderr=False,
            )
            kconf.load_config(str(config_path))

            # Normalize config outputs
            kconf.write_config(str(config_path))
            kconf.write_autoconf(str(output_path))
            return kconf
        except Exception as exc:
            print(f"Failed to parse Kconfig: {exc}")
            return None

    @classmethod
    def _wrap_output(cls, output_path: Path) -> str:
        """Wrap generated header output.

        Args:
            output_path: Resolved generated header file path.
        """
        # Read generated autoconf header content
        content = output_path.read_text(encoding="utf-8")

        # Remove `CONFIG_` prefix from generated macro names
        content = content.replace("#define CONFIG_", "#define ")

        # Build include guard
        guard = cls._build_header_guard(output_path)

        # Build timestamp
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        # Build '.h'
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

        Args:
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

        Args:
            output_path: Resolved output header file path.
            header_text: Rendered header text.

        Returns:
            `True` when the output file is written successfully, else `False`.
        """
        try:
            # Ensure parent directory exists before writing
            output_path.parent.mkdir(parents=True, exist_ok=True)

            # Write generated header text as UTF-8
            output_path.write_text(header_text, encoding="utf-8")
        except OSError:
            return False

        return True
