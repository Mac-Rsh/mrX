#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Sections plugin - insert managed linker section collection rules.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from framework.plugin import ApplicationServices, Plugin
from .sections import SectionsCommand


def create_plugin(services: ApplicationServices) -> Plugin:
    """Create the linker sections command plugin."""
    del services
    return Plugin(
        name="sections",
        version="1.0.0",
        description="Insert managed linker section rules",
        commands=(SectionsCommand(),),
    )


__all__ = ["create_plugin"]
