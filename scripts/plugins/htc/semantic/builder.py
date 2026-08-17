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
    DeleteNodeDecl,
    DeletePropertyDecl,
    Diagnostics,
    Document,
    ErrorCode,
    NodeDecl,
    NodeOverrideDecl,
    PropertyDecl,
    ReferenceValue,
    RootNode,
    SourceSpan,
    STATUS_DISABLED,
    STATUS_OKAY,
    StringValue,
    VALID_STATUS_VALUES,
    ValueExpr,
    parse_priority_status,
)
from .paths import child_path, node_segment, normalize_node_path, \
    normalize_unit_address
from .model import HardNode, HardTree, NodeProperty
from typing import Protocol


class _NodeContents(Protocol):
    """Structural contract shared by nodes and modification blocks."""

    properties: list[PropertyDecl]
    deleted_properties: list[DeletePropertyDecl]
    deleted_nodes: list[DeleteNodeDecl]
    children: list[NodeDecl]


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
        self._pending_deleted_nodes: list[tuple[str, DeleteNodeDecl]] = []

    def build(self, document: Document) -> HardTree:
        """Merge a parsed DTS document into one semantic hardware tree."""
        self._pending_deleted_properties.clear()
        self._pending_deleted_nodes.clear()
        tree = HardTree()
        for root in document.roots:
            self._merge_root(root, tree)

        root_status = tree.nodes_by_path["/"].effective_status \
            if "/" in tree.nodes_by_path else self.STATUS_OKAY
        for root in document.roots:
            for child in root.children:
                self._add_node(child, tree, parent_path="/",
                               parent_status=root_status)

        self._apply_pending_deletions(tree)
        self._apply_document_operations(tree, document)
        return tree

    def _apply_document_operations(self, tree: HardTree,
                                   document: Document) -> None:
        """Apply ordered top-level modifications after base registration."""
        operations = document.operations or [*document.delete_nodes,
                                              *document.overrides]
        for operation in operations:
            if isinstance(operation, DeleteNodeDecl):
                self._apply_delete_node(tree, operation)
                continue
            if isinstance(operation, NodeOverrideDecl):
                self._apply_override(tree, operation)
                self._apply_pending_deletions(tree)

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

        self._queue_deletions("/", root)

    def _add_node(
            self,
            node: NodeDecl,
            tree: HardTree,
            parent_path: str | None,
            parent_status: str,
    ) -> None:
        """Add one syntax node and all descendants into the semantic tree."""
        segment = node_segment(node.name, node.address)
        path = child_path(parent_path, segment)

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
            address=normalize_unit_address(node.address),
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

        self._queue_deletions(path, node)

        for child in node.children:
            self._add_node(child, tree, parent_path=hard_node.path,
                           parent_status=hard_node.effective_status)

    def _apply_delete_node(
            self,
            tree: HardTree,
            deletion: DeleteNodeDecl,
            parent_path: str | None = None,
    ) -> None:
        """Resolve and apply one `/delete-node/` directive."""
        candidates = self._resolve_delete_candidates(tree, deletion,
                                                      parent_path)
        if not candidates:
            self._warn_at(
                ErrorCode.INVALID_NODE_DECL,
                f"delete-node target not found: {deletion.target}",
                deletion.span,
            )
            return
        if len(candidates) > 1:
            self._error_at(
                ErrorCode.INVALID_NODE_DECL,
                f"delete-node target '{deletion.target}' is ambiguous: "
                f"matches {', '.join(candidates)}; use a label or full path",
                deletion.span,
            )
            return
        target_path = candidates[0]
        if target_path == "/":
            self._warn_at(
                ErrorCode.INVALID_NODE_DECL,
                "delete-node cannot remove the root node",
                deletion.span,
            )
            return
        self._remove_subtree(tree, target_path)

    def _resolve_delete_candidates(
            self,
            tree: HardTree,
            deletion: DeleteNodeDecl,
            parent_path: str | None,
    ) -> list[str]:
        """Resolve deletion targets through local names, labels, and paths."""
        target = normalize_node_path(deletion.target)
        if deletion.is_reference:
            if deletion.is_path:
                return [target] if target in tree.nodes_by_path else []
            label_path = tree.labels.get(target)
            return [label_path] if label_path is not None else []
        if deletion.is_path:
            return [target] if target in tree.nodes_by_path else []
        if parent_path is not None:
            segment = normalize_node_path(target)
            return [node.path for node in tree.nodes
                    if node.parent_path == parent_path
                    and node.path.rsplit("/", 1)[-1] == segment]
        return self._find_node_candidates(tree, target)

    def _remove_subtree(self, tree: HardTree, target_path: str) -> None:
        """Remove one node, its descendants, and all associated labels."""
        removed = tree.nodes_by_path.pop(target_path, None)
        if removed is None:
            return
        parent = tree.nodes_by_path.get(removed.parent_path or "")
        if parent is not None and target_path in parent.children:
            parent.children.remove(target_path)

        prefix = f"{target_path}/"
        for path in [path for path in tree.nodes_by_path
                     if path.startswith(prefix)]:
            tree.nodes_by_path.pop(path, None)
        for label, label_path in list(tree.labels.items()):
            if label_path == target_path or label_path.startswith(prefix):
                tree.labels.pop(label, None)

    def _apply_override(self, tree: HardTree,
                        override: NodeOverrideDecl) -> None:
        """Merge one top-level node modification into its referenced node."""
        target_path = self._resolve_override_target(tree, override.target)
        if target_path is None:
            self._error_at(
                ErrorCode.UNDEFINED_LABEL,
                f"node modification target not found: "
                f"&{override.target.reference}",
                override.span,
            )
            return
        self._merge_node_contents(
            tree,
            target_path,
            override,
        )

    @staticmethod
    def _resolve_override_target(
            tree: HardTree, target: ReferenceValue) -> str | None:
        """Resolve a node modification target by exact label or path."""
        if target.is_path:
            path = normalize_node_path(target.reference)
            return path if path in tree.nodes_by_path else None
        return tree.labels.get(target.reference)

    def _merge_node_contents(
            self,
            tree: HardTree,
            target_path: str,
            contents: _NodeContents,
    ) -> None:
        """Apply deletions and declarations to one existing semantic node."""
        target = tree.nodes_by_path.get(target_path)
        if target is None:
            return

        deleted_names = {
            deletion.name for deletion in contents.deleted_properties
        }
        if deleted_names:
            target.properties = [prop for prop in target.properties
                                 if prop.name not in deleted_names]
        for deletion in contents.deleted_nodes:
            self._apply_delete_node(tree, deletion, target_path)

        for prop in contents.properties:
            target.properties = [existing for existing in target.properties
                                 if existing.name != prop.name]
            target.properties.append(
                NodeProperty(prop.name, prop.value, prop.span))

        self._refresh_node_metadata(tree, target_path)
        target = tree.nodes_by_path.get(target_path)
        if target is None:
            return
        for child in contents.children:
            path = child_path(
                target_path, node_segment(child.name, child.address))
            if path in tree.nodes_by_path:
                self._merge_existing_node(tree, path, child)
                continue
            self._add_node(child, tree, parent_path=target_path,
                           parent_status=target.effective_status)

    def _merge_existing_node(self, tree: HardTree, path: str,
                             node: NodeDecl) -> None:
        """Merge a child declaration into an existing semantic node."""
        existing = tree.nodes_by_path[path]
        if node.label:
            label_path = tree.labels.get(node.label)
            if label_path is not None and label_path != path:
                self._error_at(ErrorCode.DUPLICATE_LABEL,
                               f"duplicate label: {node.label}", node.span)
            else:
                tree.labels[node.label] = path
                existing.label = node.label
        self._merge_node_contents(
            tree,
            path,
            node,
        )

    def _refresh_node_metadata(self, tree: HardTree, path: str) -> None:
        """Recompute compatible and status data after a node modification."""
        node = tree.nodes_by_path.get(path)
        if node is None:
            return
        syntax_properties = [
            PropertyDecl(prop.name, prop.value, prop.span)
            for prop in node.properties
        ]
        syntax_node = NodeDecl(name=node.name, properties=syntax_properties,
                               span=node.span)
        node.compatible = self._extract_compatible(path, syntax_node)
        node.declared_status = self._get_status(syntax_node)
        parent = tree.nodes_by_path.get(node.parent_path or "")
        node.effective_status = self.STATUS_DISABLED \
            if parent is not None \
            and parent.effective_status == self.STATUS_DISABLED \
            else node.declared_status
        self._refresh_descendant_statuses(tree, node)

    def _refresh_descendant_statuses(self, tree: HardTree,
                                     parent: HardNode) -> None:
        """Propagate an updated effective status through descendants."""
        for child_path in parent.children:
            child = tree.nodes_by_path.get(child_path)
            if child is None:
                continue
            child.effective_status = self.STATUS_DISABLED \
                if parent.effective_status == self.STATUS_DISABLED \
                else child.declared_status
            self._refresh_descendant_statuses(tree, child)

    def _apply_deleted_properties(self, tree: HardTree) -> None:
        """Apply recorded `/delete-property/` directives to the semantic tree."""
        pending = self._pending_deleted_properties
        self._pending_deleted_properties = {}
        for path, prop_names in pending.items():
            node = tree.nodes_by_path.get(path)
            if node is None:
                continue
            for prop_name in prop_names:
                node.properties = [
                    prop for prop in node.properties if prop.name != prop_name
                ]
            self._refresh_node_metadata(tree, path)

    def _queue_deletions(self, path: str, contents: _NodeContents) -> None:
        """Queue local deletions until the base subtree has been registered."""
        if contents.deleted_properties:
            self._pending_deleted_properties.setdefault(path, []).extend(
                deletion.name for deletion in contents.deleted_properties)
        self._pending_deleted_nodes.extend(
            (path, deletion) for deletion in contents.deleted_nodes)

    def _apply_pending_deletions(self, tree: HardTree) -> None:
        """Apply and clear deferred property and child-node deletions."""
        self._apply_deleted_properties(tree)
        pending = self._pending_deleted_nodes
        self._pending_deleted_nodes = []
        for parent_path, deletion in pending:
            self._apply_delete_node(tree, deletion, parent_path)

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
