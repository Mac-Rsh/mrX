#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""HTC Semantics - Semantic analysis for Hardware Tree Compiler.

Processes the AST and creates flat data structures for code generation.

@copyright (c) 2024-2026, MacRsh
@license SPDX-License-Identifier: Apache-2.0
"""

from .ast import ASTNode, ArrayValue, PhandleValue, PropertyNode, StringValue
from .diagnostics import Diagnostics, ErrorCode
from typing import Any, Callable, Dict, List, Optional
from dataclasses import dataclass, field


@dataclass
class NodeInfo:
    """Flat node information for code generation.

    Attributes:
        path: Full path like "/gpio/gpio0" or "/i2c/aht10"
        parent: Parent node name (None for root's direct children)
        node: Reference to original ASTNode
    """

    # Property names
    PROP_COMPATIBLE = "compatible"
    PROP_STATUS = "status"

    # Status values
    STATUS_OKAY = "okay"
    STATUS_DISABLED = "disabled"
    STATUS_EARLY = "early"
    STATUS_LAST = "last"
    STATUS_DEFER = "defer"

    # Special node
    NODE_ALIASES = "/aliases"

    path: str
    parent: Optional[str]
    node: ASTNode
    compat: List[str] = field(default_factory=list)

    @property
    def name(self) -> str:
        """Get the node name."""
        return self.node.name

    @property
    def label(self) -> Optional[str]:
        """Get the label for this node (None if no label)."""
        return self.node.label

    @property
    def addr(self) -> Optional[str]:
        """Get the address string for this node (None if no address)."""
        return self.node.address

    @property
    def status(self) -> str:
        """Get the status value for this node (defaults to 'okay')."""
        status_prop = self.node.get_property(self.PROP_STATUS)
        if status_prop and isinstance(status_prop.value, StringValue):
            return status_prop.value.value
        return self.STATUS_OKAY

    @property
    def compatible(self) -> List[str]:
        """Get the compatible strings for this node (empty list if no compatible)."""
        return self.compat

    @property
    def properties(self) -> List[PropertyNode]:
        """Get the properties for this node."""
        return self.node.properties

    def get_property(self, name: str) -> Optional[PropertyNode]:
        """Get a property by name.

        Args:
            name: Property name.

        Returns:
            PropertyNode if found, None otherwise.
        """
        return self.node.get_property(name)


@dataclass
class SymbolTable:
    """Symbol tables for semantic analysis.

    Attributes:
        all_nodes: Dict mapping path -> NodeInfo
        labels: Dict mapping label name -> NodeInfo
        map: Dict mapping reference (label/path) -> NodeInfo (unified lookup)
        aliases: Dict mapping alias name -> NodeInfo (target node)
    """

    all_nodes: Dict[str, NodeInfo] = field(default_factory=dict)
    labels: Dict[str, NodeInfo] = field(default_factory=dict)
    map: Dict[str, NodeInfo] = field(default_factory=dict)
    aliases: Dict[str, NodeInfo] = field(default_factory=dict)


class SemanticAnalyzer:
    """Semantic analyzer for hardware tree AST.

    Responsibilities:
    - Build flat symbol tables from hierarchical AST
    - Verify label uniqueness
    - Verify phandle references
    """

    # Module constants
    _PATH_SEPARATOR = "/"

    def __init__(self, diagnostics: Diagnostics) -> None:
        """Initialize the semantic analyzer.

        Args:
            diagnostics: Diagnostics instance for error reporting.
        """
        self.diagnostics: Diagnostics = diagnostics

    def analyze(self, root: ASTNode) -> SymbolTable:
        """Analyze AST and build symbol tables.

        Args:
            root: Root ASTNode from parser.

        Returns:
            SymbolTable containing all semantic information.
        """
        table = SymbolTable()

        # Walk tree and build symbol tables
        self._walk_node(
            root, "", None, NodeInfo.STATUS_OKAY, table
        )

        # Verify phandle references
        self._verify_references(table)

        # Collect aliases
        self._collect_aliases(table)

        return table

    def _walk_node(
            self,
            node: ASTNode,
            path: str,
            parent: Optional[str],
            parent_status: str,
            table: SymbolTable,
    ) -> None:
        """Recursively walk AST and build symbol tables.

        Args:
            node: Current ASTNode.
            path: Current path (parent path).
            parent: Parent path string.
            parent_status: Parent node's status.
            table: SymbolTable to populate.
        """
        # Skip root node for symbol table entries
        if node.is_root:
            for child in node.children:
                self._walk_node(
                    child, "", None, NodeInfo.STATUS_OKAY, table
                )
            return

        # Build node path
        node_path = f"{path}{self._PATH_SEPARATOR}{node.name}"

        # Get node's own status and apply parent status propagation
        node_status = self._propagate_disabled(
            node, node_path, parent_status, self._get_status(node)
        )

        # Create NodeInfo and register in symbol table
        node_info = NodeInfo(path=node_path, parent=parent, node=node)
        self._register_node(node_info, table)

        # Recursively process children
        for child in node.children:
            self._walk_node(child, node_path, node_path, node_status, table)

    @staticmethod
    def _get_status(node: ASTNode) -> str:
        """Extract the status value from a node.

        Args:
            node: ASTNode to check.

        Returns:
            Status string (defaults to STATUS_OKAY).
        """
        status_prop = node.get_property(NodeInfo.PROP_STATUS)
        if status_prop and isinstance(status_prop.value, StringValue):
            return status_prop.value.value
        return NodeInfo.STATUS_OKAY

    def _propagate_disabled(
            self, node: ASTNode, node_path: str, parent_status: str,
            node_status: str
    ) -> str:
        """Apply parent's disabled status to child if necessary.

        Args:
            node: Child ASTNode.
            node_path: Full path of the child node.
            parent_status: Status of the parent node.
            node_status: Original status of the child node.

        Returns:
            The (possibly modified) status for the child node.
        """
        if parent_status == NodeInfo.STATUS_DISABLED and node_status != NodeInfo.STATUS_DISABLED:
            # Remove existing status property if present
            existing_status = node.get_property(NodeInfo.PROP_STATUS)
            if existing_status:
                node.properties.remove(existing_status)

            # Add new disabled status property
            node.properties.append(
                PropertyNode(
                    name=NodeInfo.PROP_STATUS,
                    value=StringValue(NodeInfo.STATUS_DISABLED),
                )
            )

            self._report_at(
                self.diagnostics.warning,
                ErrorCode.CHILD_OF_DISABLED_NODE,
                f"Node '{node_path}' is a child of disabled node, "
                f"status changed to disabled",
                node,
            )
            return NodeInfo.STATUS_DISABLED

        return node_status

    def _register_node(self, node_info: NodeInfo, table: SymbolTable) -> None:
        """Register a node in the symbol table.

        Args:
            node_info: NodeInfo to register.
            table: SymbolTable to populate.
        """
        # Check for duplicate nodes
        if node_info.path in table.all_nodes:
            self._report_at(
                self.diagnostics.error,
                ErrorCode.DUPLICATE_NODE,
                f"Duplicate node path: {node_info.path}",
                node_info.node,
            )
        else:
            table.all_nodes[node_info.path] = node_info

        # Register label if present
        if node_info.node.label:
            if node_info.node.label in table.labels:
                self._report_at(
                    self.diagnostics.error,
                    ErrorCode.DUPLICATE_LABEL,
                    f"Duplicate label: {node_info.node.label}",
                    node_info.node,
                )
            else:
                table.labels[node_info.node.label] = node_info
                # Also add to unified map
                table.map[node_info.node.label] = node_info

        # Add path to unified map
        table.map[node_info.path] = node_info

        # Process compatible property
        self._extract_compatible(node_info)

    def _extract_compatible(self, node_info: NodeInfo) -> None:
        """Extract and store the compatible property strings.

        Args:
            node_info: NodeInfo to process.
        """
        # Get compatible property
        compatible_prop = node_info.node.get_property(NodeInfo.PROP_COMPATIBLE)
        if not compatible_prop:
            return

        # Accept string or array of strings
        value = compatible_prop.value
        if isinstance(value, StringValue):
            node_info.compat.append(value.value)
        elif isinstance(value, ArrayValue):
            for elem in value.elements:
                if isinstance(elem, StringValue):
                    node_info.compat.append(elem.value)
                else:
                    self._report_at(
                        self.diagnostics.warning,
                        ErrorCode.INVALID_COMPATIBLE_VALUE,
                        f"Node '{node_info.path}': compatible has non-string value",
                        node_info.node,
                    )
                    return
        else:
            self._report_at(
                self.diagnostics.warning,
                ErrorCode.INVALID_COMPATIBLE_VALUE,
                f"Node '{node_info.path}': compatible must be string or string array",
                node_info.node,
            )

    @staticmethod
    def _report_at(
            report_func: Callable[[ErrorCode, str, Optional["SourceSpan"]], "Diagnostic"],
            code: ErrorCode,
            message: str,
            node: ASTNode,
    ) -> None:
        """Report a diagnostic (error or warning) at a node's location.

        Args:
            report_func: Function to call (diagnostics.error or diagnostics.warning).
            code: Error code.
            message: Diagnostic message.
            node: ASTNode with location information.
        """
        if node.location:
            report_func(code, message).with_span(
                node.location.file_path,
                node.location.line,
                node.location.column,
            )
        else:
            report_func(code, message, None)

    def _verify_references(self, table: SymbolTable) -> None:
        """Verify all phandle references resolve to existing labels or paths.

        Args:
            table: SymbolTable containing map for lookups.
        """
        for node_info in table.all_nodes.values():
            for prop in node_info.node.properties:
                for phandle in self._extract_phandles(prop.value):
                    # Check if reference can be resolved
                    if self._lookup_by_ref(phandle.reference, table) is None:
                        self.diagnostics.error(
                            ErrorCode.UNDEFINED_LABEL,
                            f"Undefined reference: &{phandle.reference}",
                            None,
                        )

    @staticmethod
    def _lookup_by_ref(ref: str, table: SymbolTable) -> Optional[NodeInfo]:
        """Lookup a node by reference (label or path).

        Args:
            ref: Reference string (label or path).
            table: SymbolTable containing map for lookups.

        Returns:
            NodeInfo if found, None otherwise.
        """
        # Try direct lookup (label or path with /)
        if ref in table.map:
            return table.map[ref]

        # Syntactic sugar: &path -> &{/path}
        ref_path = '/' + ref
        return table.map.get(ref_path)

    @staticmethod
    def _extract_phandles(value: Any) -> List[PhandleValue]:
        """Extract all phandle values from a property value.

        Args:
            value: Property value (can contain nested phandles).

        Returns:
            List of PhandleValue objects.
        """
        if isinstance(value, PhandleValue):
            return [value]
        if isinstance(value, ArrayValue):
            return [elem for elem in value.elements if
                    isinstance(elem, PhandleValue)]
        return []

    def _collect_aliases(self, table: SymbolTable) -> None:
        """Collect alias references from the aliases node.

        Args:
            table: SymbolTable containing all nodes.
        """
        # Find the aliases node
        if NodeInfo.NODE_ALIASES not in table.all_nodes:
            return
        node_info = table.all_nodes[NodeInfo.NODE_ALIASES]

        # Process each property in aliases node
        for prop in node_info.node.properties:
            if isinstance(prop.value, PhandleValue):
                # Add to aliases if we found the target
                target_node_info = self._lookup_by_ref(
                    prop.value.reference, table
                )
                if target_node_info:
                    table.aliases[prop.name] = target_node_info
            else:
                # Invalid alias value: report warning
                self._report_at(
                    self.diagnostics.warning,
                    ErrorCode.INVALID_ALIAS_VALUE,
                    f"aliases property '{prop.name}' must be a phandle reference, ignoring",
                    node_info.node,
                )
