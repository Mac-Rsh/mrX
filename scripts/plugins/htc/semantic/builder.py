#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Semantic Builder - Semantic tree construction for Hardware Tree Compiler.

Resolves parsed syntax nodes into canonical paths, labels, status state, and
backend-ready semantic node records.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..common import (
    ArrayMode,
    ArrayValue,
    Diagnostics,
    Document,
    ErrorCode,
    NodeDecl,
    PropertyDecl,
    RootNode,
    SourceSpan,
    STATUS_DISABLED,
    STATUS_OKAY,
    StringValue,
    VALID_STATUS_VALUES,
    ValueExpr,
    parse_priority_status,
)
from .model import HardNode, HardTree, NodeProperty


class SemanticBuilder:
    """Builds the semantic hardware tree from parsed roots."""

    PROP_COMPATIBLE = "compatible"
    PROP_STATUS = "status"
    STATUS_OKAY = STATUS_OKAY
    STATUS_DISABLED = STATUS_DISABLED
    VALID_STATUS_VALUES = VALID_STATUS_VALUES

    @staticmethod
    def _copy_properties(properties: list[PropertyDecl]) -> list[NodeProperty]:
        """Copy syntax properties into semantic property records."""
        return [NodeProperty(prop.name, prop.value, prop.span) for prop in
                properties]

    def _verify_unique_properties(self, node: NodeDecl) -> None:
        """Report duplicate properties before semantic lookups pick one."""
        seen: set[str] = set()
        for prop in node.properties:
            if prop.name in seen:
                self._error_at(ErrorCode.INVALID_PROPERTY,
                               f"duplicate property: {prop.name}", prop.span)
                continue
            seen.add(prop.name)

    def _verify_merge_properties(self, node: HardNode,
                                 properties: list[PropertyDecl]) -> None:
        """Report duplicate properties when merging syntax into a node."""
        seen = {prop.name for prop in node.properties}
        for prop in properties:
            if prop.name not in seen:
                seen.add(prop.name)
                continue
            self._error_at(ErrorCode.INVALID_PROPERTY,
                           f"duplicate property: {prop.name}", prop.span)

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
            # Only extended arrays are supported
            if value.mode != ArrayMode.EXPANDED:
                self._warn_at(
                    ErrorCode.INVALID_COMPATIBLE_VALUE,
                    f"node '{path}': compatible must use string literals or expanded string lists",
                    value.span or prop.span,
                )
                return []

            # String lists are normalized into a list
            compatibles: list[str] = []
            for element in value.elements:
                if not isinstance(element, StringValue):
                    self._warn_at(
                        ErrorCode.INVALID_COMPATIBLE_VALUE,
                        f"node '{path}': compatible entries must be strings",
                        element.span or prop.span,
                    )
                    return []
                compatibles.append(element.value)
            return compatibles

        # Unknown compatible value
        self._warn_at(
            ErrorCode.INVALID_COMPATIBLE_VALUE,
            f"node '{path}': compatible must be a string or string list",
            value.span or prop.span,
        )
        return []

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

    def __init__(self, diagnostics: Diagnostics) -> None:
        """Initialize the semantic builder with a shared diagnostic sink."""
        self.diagnostics = diagnostics
        self._pending_deleted_properties: dict[str, list[str]] = {}

    def build(self, document: Document) -> HardTree:
        """Merge a parsed DTS document into one semantic hardware tree."""
        tree = HardTree()
        for root in document.roots:
            self._merge_root(root, tree)

        root_status = tree.nodes_by_path["/"].effective_status \
            if "/" in tree.nodes_by_path else self.STATUS_OKAY
        for root in document.roots:
            for child in root.children:
                self._add_node(child, tree, parent_path="/",
                               parent_status=root_status)

        self._apply_delete_nodes(tree, document)
        self._apply_deleted_properties(tree)
        return tree

    def _merge_root(self, root: RootNode, tree: HardTree) -> None:
        """Add or merge the logical root node into the semantic tree."""
        node = NodeDecl(name="root", properties=root.properties, span=root.span)
        self._verify_unique_properties(node)
        declared_status = self._get_status(node)

        if "/" not in tree.nodes_by_path:
            tree.nodes_by_path["/"] = HardNode(
                path="/",
                name=node.name,
                label=None,
                address=None,
                declared_status=declared_status,
                effective_status=declared_status,
                span=root.span,
                parent_path=None,
                compatible=self._extract_compatible("/", node),
                properties=self._copy_properties(root.properties),
            )
        else:
            hard_node = tree.nodes_by_path["/"]
            self._verify_merge_properties(hard_node, root.properties)
            hard_node.properties.extend(self._copy_properties(root.properties))
            hard_node.compatible.extend(self._extract_compatible("/", node))
            if hard_node.declared_status == self.STATUS_OKAY:
                hard_node.declared_status = declared_status
                hard_node.effective_status = declared_status

        # Record delete-property directives issued against the root node
        if root.deleted_properties:
            self._pending_deleted_properties.setdefault("/", []).extend(
                deletion.name for deletion in root.deleted_properties)

    def _add_node(
            self,
            node: NodeDecl,
            tree: HardTree,
            parent_path: str | None,
            parent_status: str,
    ) -> None:
        """Add one syntax node and all descendants into the semantic tree."""
        segment = self._path_segment(node)
        path = f"/{segment}" if parent_path in (None, "/") else \
            f"{parent_path}/{segment}"

        # Duplicate nodes are detected by canonical path
        if path in tree.nodes_by_path:
            self._error_at(ErrorCode.DUPLICATE_NODE,
                           f"duplicate node path: {path}", node.span)
            return

        self._verify_unique_properties(node)
        declared_status = self._get_status(node)
        if parent_status == self.STATUS_DISABLED:
            if declared_status != self.STATUS_DISABLED:
                self._warn_at(
                    ErrorCode.CHILD_OF_DISABLED_NODE,
                    f"node '{path}' inherits disabled status from its parent",
                    node.span,
                )
            effective_status = self.STATUS_DISABLED
        else:
            effective_status = declared_status

        hard_node = HardNode(
            path=path,
            name=node.name,
            label=node.label,
            address=self._normalize_unit_address(node.address),
            declared_status=declared_status,
            effective_status=effective_status,
            span=node.span,
            parent_path=parent_path,
            compatible=self._extract_compatible(path, node),
            properties=self._copy_properties(node.properties),
        )
        tree.nodes_by_path[path] = hard_node

        if parent_path is not None and parent_path in tree.nodes_by_path:
            tree.nodes_by_path[parent_path].children.append(path)

        if node.label:
            existing = tree.labels.get(node.label)
            if existing is not None:
                self._error_at(ErrorCode.DUPLICATE_LABEL,
                               f"duplicate label: {node.label}", node.span)
            else:
                tree.labels[node.label] = path

        # Record delete-property directives for this node
        if node.deleted_properties:
            self._pending_deleted_properties.setdefault(path, []).extend(
                deletion.name for deletion in node.deleted_properties)

        for child in node.children:
            self._add_node(child, tree, parent_path=hard_node.path,
                           parent_status=hard_node.effective_status)

    def _path_segment(self, node: NodeDecl) -> str:
        """Return the canonical DTS path segment including unit address."""
        if not node.address:
            return node.name
        return f"{node.name}@{self._normalize_unit_address(node.address)}"

    @staticmethod
    def _normalize_unit_address(address: str | None) -> str | None:
        """Normalize a unit address to DTS hex form without `0x`."""
        if address is None:
            return None
        parts: list[str] = []
        for part in address.split(","):
            lowered = part.strip().lower()
            if lowered.startswith("0x"):
                digits = lowered[2:]
            else:
                digits = lowered
            if not digits:
                digits = "0"
            parts.append(digits)
        return ",".join(parts)

    def _apply_delete_nodes(self, tree: HardTree,
                            document: Document) -> None:
        """Apply `/delete-node/` directives to the semantic tree."""
        for deletion in document.delete_nodes:
            candidates = self._find_node_candidates(tree, deletion.name)
            if not candidates:
                self._warn_at(
                    ErrorCode.INVALID_NODE_DECL,
                    f"delete-node target not found: {deletion.name}",
                    deletion.span,
                )
                continue
            if len(candidates) > 1:
                self._error_at(
                    ErrorCode.INVALID_NODE_DECL,
                    f"delete-node target '{deletion.name}' is ambiguous: "
                    f"matches {', '.join(candidates)}; use a full path",
                    deletion.span,
                )
                continue
            target_path = candidates[0]
            if target_path == "/":
                self._warn_at(
                    ErrorCode.INVALID_NODE_DECL,
                    "delete-node cannot remove the root node",
                    deletion.span,
                )
                continue
            removed = tree.nodes_by_path.pop(target_path, None)
            if removed is None:
                continue
            parent = tree.nodes_by_path.get(removed.parent_path or "")
            if parent is not None and target_path in parent.children:
                parent.children.remove(target_path)

            # Remove the complete subtree so no orphaned descendants remain
            # and no symbol table entry can point at a missing node.
            prefix = f"{target_path}/"
            for path in [
                    path for path in tree.nodes_by_path if path.startswith(prefix)
            ]:
                tree.nodes_by_path.pop(path, None)
            for label, label_path in list(tree.labels.items()):
                if label_path == target_path or label_path.startswith(prefix):
                    tree.labels.pop(label, None)

    def _apply_deleted_properties(self, tree: HardTree) -> None:
        """Apply recorded `/delete-property/` directives to the semantic tree."""
        for path, prop_names in self._pending_deleted_properties.items():
            node = tree.nodes_by_path.get(path)
            if node is None:
                continue
            for prop_name in prop_names:
                node.properties = [
                    prop for prop in node.properties if prop.name != prop_name
                ]

    @staticmethod
    def _find_node_candidates(tree: HardTree, name: str) -> list[str]:
        """Return unique candidate paths for a delete-node target."""
        if name in tree.nodes_by_path:
            return [name]
        full = f"/{name}"
        if full in tree.nodes_by_path:
            return [full]
        return list(dict.fromkeys(
            node.path for node in tree.nodes
            if node.name == name or node.path.endswith(f"/{name}")
        ))

    def _get_status(self, node: NodeDecl) -> str:
        """Get and validate the effective status string for a node."""
        prop = node.get_property(self.PROP_STATUS)
        if prop is None:
            return self.STATUS_OKAY

        # Status must be expressed as a string literal
        if not isinstance(prop.value, StringValue):
            self._error_at(ErrorCode.INVALID_STATUS,
                           f"{self.PROP_STATUS} must be a string literal",
                           prop.value.span or prop.span)
            return self.STATUS_OKAY
        value = prop.value.value

        # Fixed status keywords are accepted directly
        if value in self.VALID_STATUS_VALUES:
            return value

        # Priority statuses use the form `prio(n)`
        if value.startswith("prio(") and value.endswith(")"):
            try:
                priority = parse_priority_status(value)
            except ValueError:
                self._error_at(ErrorCode.INVALID_PRIO,
                               f"invalid priority status: '{value}'",
                               prop.value.span or prop.span)
                return self.STATUS_OKAY
            if priority is None:
                self._error_at(ErrorCode.INVALID_PRIO,
                               f"invalid priority status: '{value}'",
                               prop.value.span or prop.span)
                return self.STATUS_OKAY
            if priority < 0 or priority > 9:
                self._error_at(ErrorCode.INVALID_PRIO,
                               f"priority out of range in status: '{value}'",
                               prop.value.span or prop.span)
                return self.STATUS_OKAY
            return value

        # Unknown DTS status values are accepted with a warning
        self._warn_at(
            ErrorCode.INVALID_STATUS,
            f"unknown status value '{value}', treating as disabled",
            prop.value.span or prop.span,
        )
        return self.STATUS_DISABLED
