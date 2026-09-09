#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
RPC IDL plugin - compile `.idl` sources into generated C header and source.

Contains the RPC IDL command entry point together with frontend, semantic,
and C emission stages.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from framework.plugin import ApplicationServices, Plugin
from .compiler import CompileOptions, CompileResult, RpcIdlCompiler
from .rpc_idl import RpcIdlCommand


def create_plugin(services: ApplicationServices) -> Plugin:
    """Create the RPC IDL compiler command plugin."""
    del services
    return Plugin(
        name="rpc-idl",
        version="1.0.0",
        description=("Compile RPC IDL sources into generated C header and "
                     "source"),
        commands=(RpcIdlCommand(),),
    )


__all__ = [
    "CompileOptions",
    "CompileResult",
    "RpcIdlCompiler",
    "RpcIdlCommand",
    "create_plugin",
]
