#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Local command plugin entry point for small utility commands.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from framework.plugin import ApplicationServices, Plugin
from .echo import EchoCommand


def create_plugin(services: ApplicationServices) -> Plugin:
    """Create the small utility command plugin."""
    del services
    return Plugin(
        name="commands",
        version="1.0.0",
        description="Small utility commands",
        commands=(EchoCommand(),),
    )


__all__ = ["create_plugin"]
