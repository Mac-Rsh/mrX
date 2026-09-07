#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
MDK plugin - add, delete, sync, or rename a uVision project.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from framework.plugin import ApplicationServices, Plugin
from .mdk import MDKCommand


def create_plugin(services: ApplicationServices) -> Plugin:
    """Create the MDK project command plugin."""
    del services
    return Plugin(
        name="mdk",
        version="1.0.0",
        description="Add, delete, sync, or rename an MDK project",
        commands=(MDKCommand(),),
    )


__all__ = ["create_plugin"]
