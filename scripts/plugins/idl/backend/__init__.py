#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IDL Backend Package - Public backend exports for the RPC IDL compiler.

Exposes C name mapping, identifier collision checks, and header and source
rendering helpers.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .symbols import CSymbolChecker
from .source import SourceBuilder
from .header import HeaderBuilder
from .names import CNames

__all__ = ["CNames", "CSymbolChecker", "HeaderBuilder", "SourceBuilder"]
