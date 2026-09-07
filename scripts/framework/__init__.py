#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Public exports for the mrX command framework.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .application import Application, create_application
from .command import Command, CommandResult, CommandSpec
from .completion import CompletionItem, CompletionRequest
from .completion import DIRECTORY_COMPLETION, FILE_COMPLETION, PATH_COMPLETION
from .completion import attach_path_completion
from .console import Console
from .context import CommandContext
from .diagnostics import Diagnostic, DiagnosticLevel
from .parser import CommandParser
from .plugin import ApplicationServices, Plugin
from .registry import CommandRegistry
from .runner import CommandRunner

__all__ = [
    "Application",
    "ApplicationServices",
    "Command",
    "CommandContext",
    "CommandParser",
    "CommandRegistry",
    "CommandResult",
    "CommandRunner",
    "CommandSpec",
    "CompletionItem",
    "CompletionRequest",
    "Console",
    "DIRECTORY_COMPLETION",
    "Diagnostic",
    "DiagnosticLevel",
    "FILE_COMPLETION",
    "PATH_COMPLETION",
    "Plugin",
    "attach_path_completion",
    "create_application",
]
