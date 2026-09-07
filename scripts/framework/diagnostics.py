#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Structured diagnostics shared by plugin loading and command execution.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from dataclasses import dataclass
from enum import StrEnum


class DiagnosticLevel(StrEnum):
    """Severity levels supported by command diagnostics."""

    INFO = "info"
    WARNING = "warning"
    ERROR = "error"


@dataclass(slots=True, frozen=True)
class Diagnostic:
    """One structured message produced while loading or running a command."""

    level: DiagnosticLevel
    code: str
    message: str
    hint: str = ""
    details: tuple[str, ...] = ()
