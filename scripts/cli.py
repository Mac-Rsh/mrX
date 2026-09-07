#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
mrX command-line entry point.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from framework.application import create_application
import sys


def main() -> int:
    """Run the mrX command-line interface."""
    return create_application().run(sys.argv[1:])


if __name__ == "__main__":
    raise SystemExit(main())
