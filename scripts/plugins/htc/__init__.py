#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC plugin - compile `.hts`/`.htsi` sources into generated headers.

Contains the HTC command entry point together with frontend, semantic,
validation, backend, and shared compiler support modules.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from framework.plugin import ApplicationServices, Plugin
from .compiler import CompileOptions, CompileResult, HTCCompiler
from .htc import HTCCommand


def create_plugin(services: ApplicationServices) -> Plugin:
    """Create the HTC compiler command plugin."""
    del services
    return Plugin(
        name="htc",
        version="1.0.0",
        description="Compile HTS sources into generated headers",
        commands=(HTCCommand(),),
    )


__all__ = [
    "CompileOptions",
    "CompileResult",
    "HTCCompiler",
    "HTCCommand",
    "create_plugin",
]
