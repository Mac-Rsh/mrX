#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Package - Public HTC compiler command and pipeline exports.

Contains the HTC command entry point together with frontend, semantic,
validation, backend, and shared compiler support modules.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .compiler import CompileOptions, CompileResult, HTCCompiler

__all__ = ["CompileOptions", "CompileResult", "HTCCompiler"]
