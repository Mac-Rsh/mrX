#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
mrX configuration management module

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..module import MrModule, MrLogger, argparse
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
    log_level = MrLogger.LEVEL_INF

    def add_arguments(self, parser: argparse.ArgumentParser):
        """Add module-specific arguments.

        Args:
            parser: Argument parser to add arguments to.
        """
        # Kconfig file path
        parser.add_argument(
            "--kconfig",
            type=str,
            default="Kconfig",
            help="Kconfig file path (default: Kconfig)",
        )
        # .config file path
        parser.add_argument(
            "--config",
            type=str,
            default=".config",
            help=".config file path (default: .config)",
        )
        # Output header file path
        parser.add_argument(
            "--output",
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

    def run(self, args: argparse.Namespace):
        """Execute Kconfig processing.

        Args:
            args: Parsed command line arguments.

        Returns:
            None
        """
        # Run menuconfig
        if not args.no_ui:
            self._run_menuconfig()

        # Parse arguments
        self._parse_arguments(args)

        # Parse Kconfig
        self._parse_kconfig()

        # Generate header
        self._generate_header()

    def _parse_arguments(self, args: argparse.Namespace) -> None:
        """Parse and validate command line arguments.

        Args:
            args: Parsed command line arguments.

        Returns:
            None

        Raises:
            FileNotFoundError: If Kconfig or .config file does not exist.
        """
        # Validate Kconfig file
        self.kconfig_path = Path(args.kconfig)
        if not self.kconfig_path.exists():
            self.logger.err(f"Kconfig file not found: {args.kconfig}")
            raise FileNotFoundError(f"Kconfig file not found: {args.kconfig}")

        # Validate .config file
        self.config_path = Path(args.config)
        if not self.config_path.exists():
            self.logger.err(f"Config file not found: {args.config}")
            raise FileNotFoundError(f"Config file not found: {args.config}")

        # Set output path(if it doesn't exist, it will be created automatically)
        self.output_path = Path(args.output)

    def _run_menuconfig(self) -> None:
        """Run menuconfig for interactive configuration.

        Returns:
            None

        Raises:
            RuntimeError: If menuconfig execution fails.
        """
        try:
            subprocess.run(
                ["menuconfig"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
            self.logger.inf("Menuconfig completed")
        except FileNotFoundError:
            self.logger.err("menuconfig command not found")
            raise
        except Exception as e:
            self.logger.err(f"Failed to run menuconfig: {e}")
            raise

    def _parse_kconfig(self) -> None:
        """Parse Kconfig file and load configuration.

        Returns:
            None

        Raises:
            Exception: If Kconfig parsing fails.
        """
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
            self.logger.inf("Kconfig parsed")
        except Exception as e:
            self.logger.err(f"Failed to parse Kconfig: {e}")
            raise

    def _generate_header(self) -> None:
        """Generate C header file from Kconfig configuration.

        Reads auto.conf, removes CONFIG_ prefix, and wraps with header guard.

        Returns:
            None

        Raises:
            IOError: If file operations fail.
        """
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
        self.logger.inf("Header generated")
