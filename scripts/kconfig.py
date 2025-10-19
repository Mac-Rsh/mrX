#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
@copyright (c) 2024-2025, MacRsh

@license SPDX-License-Identifier: Apache-2.0

@date 2024-09-06        MacRsh        First version
"""

from kconfiglib import Kconfig
from datetime import datetime
from typing import Optional
from pathlib import Path
import subprocess
import argparse
import logging
import sys
import re


class ConfigGenerator:
    """
    A class to handle kconfig configuration generation.
    """

    def __init__(self, kconfig_path: Optional[Path] = None,
                 config_path: Optional[Path] = None,
                 output_path: Optional[Path] = None):
        """
        Initialize the configuration generator.

        Args:
            kconfig_path (Path, optional): Path to Kconfig file.
            config_path (Path, optional): Path to configuration file.
            output_path (Path, optional): Path to output header file.
        """
        # Set default values
        self.kconfig_path = kconfig_path or Path('Kconfig')
        self.config_path = config_path or Path('.config')
        self.output_path = output_path or Path('include/mr_config.h')

        # Setup logging
        logging.basicConfig(level=logging.INFO,
                            format="[%(levelname)s]: %(message)s")
        self.logger = logging.getLogger(__name__)

    def run_menuconfig(self) -> None:
        """
        Run menuconfig.
        """
        try:
            # Run 'menuconfig'
            subprocess.run(['menuconfig'], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, check=True)
        except subprocess.CalledProcessError as e:
            self.logger.error(f"Menuconfig failed: {e}")
            sys.exit(1)

    def generate_config(self) -> None:
        """
        Generate configuration file from Kconfig.
        """
        try:
            # Load 'Kconfig'
            kconf = Kconfig(str(self.kconfig_path), warn=False,
                            warn_to_stderr=False)
            kconf.load_config(str(self.config_path))
            kconf.write_autoconf(str(self.output_path))

            # Generate config file
            self._export_header()
            self.logger.info(
                f"Configuration file generated: {self.output_path}")
        except Exception as e:
            self.logger.error(f"Generate config failed: {e}")
            sys.exit(1)

    def _export_header(self) -> None:
        """
        Process the configuration file.
        """
        with open(self.output_path, 'r+') as f:
            content = f.read()
            f.truncate(0)
            f.seek(0)

            # Write modified header with guards and extern C
            content = (f"/* Auto-generated configuration file */\n"
                       f"/* Generated on: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')} */\n\n"
                       "#ifndef __MR_CONFIG_H__\n"
                       "#define __MR_CONFIG_H__\n\n"
                       "#ifdef __cplusplus\n"
                       "extern \"C\" {\n"
                       "#endif /* __cplusplus */\n\n"
                       f"{content.replace("#define CONFIG_", "#define ")}"
                       "\n#ifdef __cplusplus\n"
                       "}\n"
                       "#endif /* __cplusplus */\n\n"
                       "#endif /* __MR_CONFIG_H__ */\n")
            f.write(content)

    @classmethod
    def create_from_args(cls):
        """
        Create an instance of the class from command line arguments.

        Returns:
            ConfigGenerator: ConfigGenerator instance.
        """
        parser = argparse.ArgumentParser(
            description="Generate configuration files from Kconfig.",
            epilog="Example: python kconfig.py -k Kconfig -c .config -f include/mr_config.h"
        )
        parser.add_argument(
            "-k", "--kconfig",
            type=Path,
            default=Path("Kconfig"),
            help="Path to Kconfig file (default: Kconfig)"
        )
        parser.add_argument(
            "-c", "--config",
            type=Path,
            default=Path(".config"),
            help="Configuration file (default: .config)"
        )
        parser.add_argument(
            "-f", "--file",
            type=Path,
            default=Path("include/mr_config.h"),
            help="Output file (default: include/mr_config.h)"
        )
        args = parser.parse_args()
        return cls(args.kconfig, args.config, args.file)


def main() -> None:
    """
    Main entry point for the script.
    """
    config_generator = ConfigGenerator.create_from_args()
    config_generator.run_menuconfig()
    config_generator.generate_config()


if __name__ == "__main__":
    main()
