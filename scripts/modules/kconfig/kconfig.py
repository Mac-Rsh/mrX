#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
mrX configuration management module

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..module import MrModule, argparse
from datetime import datetime
from pathlib import Path
import subprocess
import kconfiglib


class Kconfig(MrModule):
    """Kconfig configuration file processor."""

    name: str = "kconfig"
    description: str = "Process Kconfig and generate C header"
    note: str = "Example: python mrXtools.py kconfig"
    version: str = "1.0.0"
    author: str = "MacRsh"

    def add_arguments(self, parser: argparse.ArgumentParser):
        """Add module-specific arguments.

        Args:
            parser: Argument parser to add arguments to.
        """
        # Kconfig file path
        parser.add_argument(
            "-k", "--kconfig",
            type=str,
            default="Kconfig",
            help="Kconfig file path (default: Kconfig)",
        )
        # .config file path
        parser.add_argument(
            "-c", "--config",
            type=str,
            default=".config",
            help=".config file path (default: .config)",
        )
        # Output header file path
        parser.add_argument(
            "-o", "--output",
            type=str,
            default="include/mrX/mr_config.h",
            help="Output header file path (default: include/mrX/mr_config.h)",
        )
        # Disable menuconfig UI
        parser.add_argument(
            "--no_ui",
            action="store_true",
            help="Disable UI (default: False)",
        )

    def run(self, args: argparse.Namespace) -> int:
        """Execute Kconfig processing.

        Args:
            args: Parsed command line arguments.

        Returns:
            None
        """
        # Show menuconfig UI
        if not args.no_ui:
            self._show_ui()

        # Parse arguments
        self._parse_arguments(args)

        # Parse Kconfig
        self._parse_kconfig()

        # Generate header
        self._generate_header()
        return 0

    def _parse_arguments(self, args: argparse.Namespace) -> None:
        """Parse and validate command line arguments."""
        # Validate Kconfig file
        self.kconfig_path = Path(args.kconfig)
        if not self.kconfig_path.exists():
            raise FileNotFoundError(f"Kconfig file not found: {args.kconfig}")

        # Validate .config file
        self.config_path = Path(args.config)
        if not self.config_path.exists():
            raise FileNotFoundError(f"Config file not found: {args.config}")

        # Set output path(if it doesn't exist, it will be created automatically)
        self.output_path = Path(args.output)

    @staticmethod
    def _show_ui() -> None:
        """Show menuconfig UI."""
        try:
            subprocess.run(
                ["menuconfig"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
        except FileNotFoundError:
            raise FileNotFoundError("Showing menuconfig failed. Please make sure menuconfig is installed.")
        except Exception as e:
            raise Exception(f"Showing menuconfig failed: {e}")

    def _parse_kconfig(self) -> None:
        """Parse Kconfig file and load configuration."""
        try:
            # Load Kconfig
            kconf = kconfiglib.Kconfig(self.kconfig_path, warn=False,
                                       warn_to_stderr=False)
            # Load .config file
            kconf.load_config(self.config_path)
            # Write config back to .config
            kconf.write_config(self.config_path)
            # Write to output file
            kconf.write_autoconf(self.output_path)
        except Exception as e:
            raise Exception(f"Failed to parse Kconfig: {e}")

    def _generate_header(self) -> None:
        """Generate C header file from Kconfig configuration."""
        with open(self.output_path, "r+") as f:
            # Generate header guard macro name
            output_upper = f"{self.output_path.name.upper().replace('.', '_')}"

            # Read existing content
            content = f.read()
            # Clear file
            f.truncate(0)
            f.seek(0)

            # Remove CONFIG_ prefix from defines
            content = content.replace("#define CONFIG_", "#define ")

            # Write header guard start
            f.write(f"#ifndef __{output_upper}__\n")
            f.write(f"#define __{output_upper}__\n\n")

            # Write timestamp
            ts = datetime.now().strftime("%Y-%m-%d-%H:%M:%S")
            f.write(f"\n/* Generated on {ts} */\n")

            # Write C++ guard
            f.write("#ifdef __cplusplus\n")
            f.write("extern \"C\" {\n")
            f.write("#endif /* __cplusplus */\n\n")

            # Write configuration content
            f.write(content)

            # Write C++ guard end
            f.write("\n#ifdef __cplusplus\n")
            f.write("}\n")
            f.write("#endif /* __cplusplus */\n\n")

            # Write header guard end
            f.write(f"#endif /* __{output_upper}__ */\n")
        MrModule.print(f"Generated header: {self.output_path}")
