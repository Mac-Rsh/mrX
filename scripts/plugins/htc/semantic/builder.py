#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Semantic Builder - Semantic tree construction for Hardware Tree Compiler.

Resolves parsed syntax nodes into canonical paths, labels, status state, and
backend-ready semantic node records.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..common import ArrayMode, ArrayValue, Diagnostics, ErrorCode, \
    NodeDecl, RootNode, SourceSpan, StringValue, ValueExpr
from dataclasses import dataclass, field
from collections import OrderedDict


@dataclass(frozen=True)
class ResolvedProperty:
    """Semantic property record copied from syntax into the resolved tree."""

    name: str
    value: ValueExpr
    span: SourceSpan | None


@dataclass
class ResolvedNode:
    """A node after path resolution and effective status calculation."""

    path: str
    name: str
    label: str | None
    address: str | None
    declared_status: str
    effective_status: str
    span: SourceSpan | None
    parent_path: str | None = None
    compatible: list[str] = field(default_factory=list)
    properties: list[ResolvedProperty] = field(default_factory=list)
    children: list[str] = field(default_factory=list)


@dataclass
class HardwareTree:
    """Semantic IR consumed by validation and backend stages."""

    nodes_by_path: "OrderedDict[str, ResolvedNode]" = field(
        default_factory=OrderedDict)
    labels: dict[str, str] = field(default_factory=dict)
    aliases: dict[str, str] = field(default_factory=dict)

    @property
    def nodes(self) -> list[ResolvedNode]:
        return list(self.nodes_by_path.values())


class SemanticBuilder:
    """Builds the resolved semantic hardware tree from parsed roots."""

    PROP_COMPATIBLE = "compatible"
    PROP_STATUS = "status"
    STATUS_PRIO_PREFIX = "prio("
    STATUS_PRIO_SUFFIX = ")"
    STATUS_OKAY = "okay"
    STATUS_DISABLED = "disabled"
    STATUS_EARLY = "early"
    STATUS_LAST = "last"
    STATUS_DEFER = "defer"
    VALID_STATUS_VALUES = {
        STATUS_OKAY,
        STATUS_DISABLED,
        STATUS_EARLY,
        STATUS_LAST,
        STATUS_DEFER,
    }

    def __init__(self, diagnostics: Diagnostics) -> None:
        """Initialize the semantic builder with a shared diagnostic sink."""
        self.diagnostics = diagnostics

    def build(self, roots: list[RootNode]) -> HardwareTree:
        """Merge parsed roots into one semantic hardware tree."""
        # The semantic stage merges multiple parsed roots into one logical tree
        tree = HardwareTree()
        for root in roots:
            for child in root.children:
                self._register_node(child, tree, parent_path=None,
                                    parent_status=self.STATUS_OKAY)

        return tree

    def _register_node(
            self,
            node: NodeDecl,
            tree: HardwareTree,
            parent_path: str | None,
            parent_status: str,
    ) -> None:
        """Register one syntax node and all descendants into the semantic tree."""
        # Paths are canonical identifiers in the semantic IR
        path = f"/{node.name}" if parent_path is None else f"{parent_path}/{node.name}"

        # Effective status is inherited from the parent when disabled
        declared_status = self._read_status(node)
        effective_status = self._resolve_effective_status(parent_status,
                                                          declared_status)
        if self._inherits_disabled_status(parent_status, declared_status):
            self._warn_at(
                ErrorCode.CHILD_OF_DISABLED_NODE,
                f"node '{path}' inherits disabled status from its parent",
                node.span,
            )

        # Duplicate nodes are detected by canonical path
        if path in tree.nodes_by_path:
            self._error_at(ErrorCode.DUPLICATE_NODE,
                           f"duplicate node path: {path}", node.span)
            return

        # Register the resolved node record
        resolved = ResolvedNode(
            path=path,
            name=node.name,
            label=node.label,
            address=node.address,
            declared_status=declared_status,
            effective_status=effective_status,
            span=node.span,
            parent_path=parent_path,
            compatible=self._extract_compatible(path, node),
            properties=[ResolvedProperty(prop.name, prop.value, prop.span) for
                        prop in node.properties],
        )
        tree.nodes_by_path[path] = resolved

        # Link the node back into its parent list when applicable
        if parent_path is not None and parent_path in tree.nodes_by_path:
            tree.nodes_by_path[parent_path].children.append(path)

        # Labels populate a second symbol table for later reference lookup
        if node.label:
            existing = tree.labels.get(node.label)
            if existing is not None:
                self._error_at(ErrorCode.DUPLICATE_LABEL,
                               f"duplicate label: {node.label}", node.span)
            else:
                tree.labels[node.label] = path

        # Register children after the parent is visible in the tree
        for child in node.children:
            self._register_node(child, tree, parent_path=path,
                                parent_status=effective_status)

    def _resolve_effective_status(self, parent_status: str,
                                  declared_status: str) -> str:
        """Resolve the effective status for one node from parent and local state."""
        if parent_status == self.STATUS_DISABLED:
            return self.STATUS_DISABLED
        return declared_status

    def _inherits_disabled_status(self, parent_status: str,
                                  declared_status: str) -> bool:
        """Return True when a node becomes disabled because its parent is disabled."""
        return parent_status == self.STATUS_DISABLED and declared_status != self.STATUS_DISABLED

    def _extract_compatible(self, path: str, node: NodeDecl) -> list[str]:
        """Extract normalized compatible strings from a node property."""
        prop = node.get_property(self.PROP_COMPATIBLE)
        if prop is None:
            return []
        value = prop.value

        # Single-string compatible values are normalized into a list
        if isinstance(value, StringValue):
            return [value.value]

        # Array compatible values must contain only strings
        if isinstance(value, ArrayValue):
            # compatible-rule: inline arrays are reserved for brace initializers.
            if value.mode != ArrayMode.EXPANDED:
                self._warn_at(
                    ErrorCode.INVALID_COMPATIBLE_VALUE,
                    f"node '{path}': compatible must use string literals or expanded string lists",
                    prop.span,
                )
                return []
            compatibles: list[str] = []
            for element in value.elements:
                if not isinstance(element, StringValue):
                    self._warn_at(
                        ErrorCode.INVALID_COMPATIBLE_VALUE,
                        f"node '{path}': compatible entries must be strings",
                        prop.span,
                    )
                    return []
                compatibles.append(element.value)
            return compatibles
        self._warn_at(
            ErrorCode.INVALID_COMPATIBLE_VALUE,
            f"node '{path}': compatible must be a string or string list",
            prop.span,
        )
        return []

    def _read_status(self, node: NodeDecl) -> str:
        """Read and validate the effective status string for a node."""
        prop = node.get_property(self.PROP_STATUS)
        if prop is None:
            return self.STATUS_OKAY

        # Status must be expressed as a string literal
        if not isinstance(prop.value, StringValue):
            self._error_at(ErrorCode.INVALID_STATUS,
                           f"{self.PROP_STATUS} must be a string literal",
                           prop.span)
            return self.STATUS_OKAY
        value = prop.value.value

        # Fixed status keywords are accepted directly
        if value in self.VALID_STATUS_VALUES:
            return value

        # Priority statuses use the form `prio(n)`
        if value.startswith(self.STATUS_PRIO_PREFIX) and value.endswith(
                self.STATUS_PRIO_SUFFIX):
            try:
                priority = int(value[len(self.STATUS_PRIO_PREFIX):-len(
                    self.STATUS_PRIO_SUFFIX)])
            except ValueError:
                self._error_at(ErrorCode.INVALID_PRIO,
                               f"invalid priority status: '{value}'", prop.span)
                return self.STATUS_OKAY
            if priority < 0 or priority > 9:
                self._error_at(ErrorCode.INVALID_PRIO,
                               f"priority out of range in status: '{value}'",
                               prop.span)
                return self.STATUS_OKAY
            return value
        self._error_at(ErrorCode.INVALID_STATUS, f"invalid status: '{value}'",
                       prop.span)
        return self.STATUS_OKAY

    def _error_at(self, code: ErrorCode, message: str,
                  span: SourceSpan | None) -> None:
        """Emit a semantic error at a specific source span."""
        diagnostic = self.diagnostics.error(code, message)
        if span is not None:
            diagnostic.with_source_span(span)

    def _warn_at(self, code: ErrorCode, message: str,
                 span: SourceSpan | None) -> None:
        """Emit a semantic warning at a specific source span."""
        diagnostic = self.diagnostics.warning(code, message)
        if span is not None:
            diagnostic.with_source_span(span)
