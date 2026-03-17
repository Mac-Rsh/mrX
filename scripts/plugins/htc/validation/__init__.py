#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Validation Package - Public validation exports for Hardware Tree Compiler.

Exposes explicit validation passes that run on semantic hardware trees.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .tree import TreeValidator

__all__ = ["TreeValidator"]
