#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Semantic Package - Public semantic exports for Hardware Tree Compiler.

Exposes resolved tree models and semantic builder entry points.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .builder import HardwareTree, ResolvedNode, SemanticBuilder

__all__ = ["HardwareTree", "ResolvedNode", "SemanticBuilder"]
