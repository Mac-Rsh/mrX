#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Reference Resolver - Shared semantic reference lookup.

Provides one lookup implementation for validation and backend emission.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .model import HardTree


class ReferenceResolver:
    """Resolves label, path, and shorthand references against a hard tree."""

    @staticmethod
    def resolve(tree: HardTree, reference: str, is_path: bool) -> str | None:
        """Resolve a reference through path, label, root shorthand, then name."""
        if is_path:
            return reference if reference in tree.nodes_by_path else None

        target = tree.labels.get(reference)
        if target is not None:
            return target

        short = f"/{reference}"
        if short in tree.nodes_by_path:
            return short

        candidates = ReferenceResolver.find_by_name(tree, reference)
        return candidates[0] if candidates else None

    @staticmethod
    def find_by_name(tree: HardTree, name: str) -> list[str]:
        """Return all canonical paths whose node name equals ``name``."""
        return [node.path for node in tree.nodes
                if node.name == name]
