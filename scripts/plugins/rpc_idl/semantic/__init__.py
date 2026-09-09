#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IDL Semantic Package - Public semantic exports for the RPC IDL compiler.

Exposes the resolved schema model and the builder entry point.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .builder import SemanticBuilder
from .model import Schema

__all__ = ["Schema", "SemanticBuilder"]
