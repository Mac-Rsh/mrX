#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Backend Package - Public backend exports for Hardware Tree Compiler.

Exposes macro lowering and header rendering helpers.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .macro import MacroBuilder

__all__ = ["MacroBuilder"]
