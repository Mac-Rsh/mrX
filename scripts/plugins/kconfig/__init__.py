#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Kconfig plugin - process Kconfig files and generate a C header.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from framework.plugin import ApplicationServices, Plugin
from .kconfig import KconfigCommand


def create_plugin(services: ApplicationServices) -> Plugin:
    """Create the Kconfig command plugin."""
    del services
    return Plugin(
        name="kconfig",
        version="1.0.0",
        description="Process Kconfig and generate a C header",
        commands=(KconfigCommand(),),
    )


__all__ = ["create_plugin"]
