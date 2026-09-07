#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Platform plugin - install, uninstall, or inspect platform files.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from framework.plugin import ApplicationServices, Plugin
from .platform import PlatformCommand


def create_plugin(services: ApplicationServices) -> Plugin:
    """Create the platform install command plugin."""
    del services
    return Plugin(
        name="platform",
        version="1.0.0",
        description="Install, uninstall, or inspect platform files",
        commands=(PlatformCommand(),),
    )


__all__ = ["create_plugin"]
