#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Semantic Package - Public semantic exports for Hardware Tree Compiler.

Exposes semantic tree models and semantic builder entry points.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .builder import HardNode, HardTree, NodeProperty, SemanticBuilder

__all__ = ["HardNode", "HardTree", "NodeProperty", "SemanticBuilder"]
