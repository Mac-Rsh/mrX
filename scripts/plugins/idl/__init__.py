#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IDL plugin - compile `.idl` sources into generated C header and source.

Contains the IDL command entry point together with frontend, semantic,
and C emission stages.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from framework.plugin import ApplicationServices, Plugin
from .compiler import CompileOptions, CompileResult, IDLCompiler
from .idl import IDLCommand


def create_plugin(services: ApplicationServices) -> Plugin:
    """Create the IDL compiler command plugin."""
    del services
    return Plugin(
        name="idl",
        version="1.0.0",
        description="Compile IDL sources into generated C header and source",
        commands=(IDLCommand(),),
    )


__all__ = [
    "CompileOptions",
    "CompileResult",
    "IDLCompiler",
    "IDLCommand",
    "create_plugin",
]
