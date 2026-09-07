#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Semantic Model - Shared semantic tree data records.

Defines the semantic IR produced by the builder and consumed by validation and
backend stages.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..common import SourceSpan, ValueExpr
from dataclasses import dataclass, field
from collections import OrderedDict


@dataclass(frozen=True)
class NodeProperty:
    """Semantic property record copied from syntax into one hardware node."""

    name: str
    value: ValueExpr
    span: SourceSpan | None


@dataclass
class HardNode:
    """A hardware node after path resolution and status calculation."""

    path: str
    name: str
    label: str | None
    address: str | None
    declared_status: str
    effective_status: str
    span: SourceSpan | None
    parent_path: str | None = None
    compatible: list[str] = field(default_factory=list)
    properties: list[NodeProperty] = field(default_factory=list)
    children: list[str] = field(default_factory=list)


@dataclass
class HardTree:
    """Semantic IR consumed by validation and backend stages."""

    nodes_by_path: "OrderedDict[str, HardNode]" = field(
        default_factory=OrderedDict)
    labels: dict[str, str] = field(default_factory=dict)
    aliases: dict[str, str] = field(default_factory=dict)

    @property
    def nodes(self) -> list[HardNode]:
        """Return hardware nodes in path insertion order."""
        return list(self.nodes_by_path.values())
