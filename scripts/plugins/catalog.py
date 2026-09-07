#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
First-party plugin catalog.

Plugins are loaded from this explicit list. Directory scanning and decorator
discovery are not used.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

BUILTIN_PLUGINS = (
    "plugins.commands",
    "plugins.kconfig",
    "plugins.htc",
    "plugins.idl",
    "plugins.platform",
    "plugins.sections",
    "plugins.mdk",
)
