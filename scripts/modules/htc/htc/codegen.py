#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Codegen - Code generation for Hardware Tree Compiler.

Converts the SymbolTable into C header macro definitions.

@copyright (c) 2024-2026, MacRsh
@license SPDX-License-Identifier: Apache-2.0
"""

from .diagnostics import Diagnostics, ErrorCode
from typing import List, Dict, Optional, Tuple, cast
from .semantics import SymbolTable, NodeInfo
from .ast import (
    Value, StringValue, NumberValue, FloatValue,
    PhandleValue, IdentifierValue, ArrayValue
)


class CodeGenerator:
    """
    Code generator for hardware tree compiler.

    Emits C header macros from SymbolTable in three phases:
    1. Node Emission - Emit macros for each enabled node
    2. Label/Alias Emission - Create LABEL and ALIAS macros
    3. INST Emission - Create INST_S_DRIVER and INST_S_<compatible> macros
    """

    _MACRO_PREFIX: str = "__MR_HT_"
    _MACRO_DEFINE_COLUMN: int = 50
    _SECTION_HEADER_MAX_LENGTH: int = 80
    _SECTION_HEADER_FIXED_PADDING: int = 32

    _PROP_NAME = "NAME"
    _PROP_INIT_LEVEL = "INIT_LEVEL"
    _PROP_PARENT = "PARENT"
    _PROP_ADDR = "ADDR"

    _SKIP_NODES: frozenset[str] = frozenset({
        "aliases",
    })

    _STATUS_TO_INIT_LEVEL: Dict[str, str] = {
        NodeInfo.STATUS_EARLY: "MR_DRIVER_LEVEL_EARLY",
        NodeInfo.STATUS_OKAY: "MR_DRIVER_LEVEL_NORMAL",
        NodeInfo.STATUS_LAST: "MR_DRIVER_LEVEL_LAST",
        NodeInfo.STATUS_DEFER: "MR_DRIVER_LEVEL_DEFER",
        NodeInfo.STATUS_DISABLED: "MR_DRIVER_LEVEL_DEFER",
    }

    # System properties to skip during property emission
    _SKIP_PROPERTIES: frozenset[str] = frozenset({
        NodeInfo.PROP_COMPATIBLE,
    })

    def __init__(self, diagnostics: Diagnostics) -> None:
        """Initialize the code generator.

        Args:
            diagnostics: Diagnostics collector for error reporting.
        """
        self.diagnostics: Diagnostics = diagnostics
        self.output: List[str] = []
        self.symbol_table: Optional[SymbolTable] = None

    def generate(self, table: SymbolTable, output_path: str) -> bool:
        """Emit complete C header output and write to file.

        Args:
            table: SymbolTable from semantic analysis.
            output_path: Path to output .h file.

        Returns:
            True if emission succeeded, False otherwise.
        """
        self.symbol_table = table
        self.output = []

        # Node Emission
        self._emit_nodes(table)

        # Label/Alias Emission
        self._emit_labels_and_aliases(table)

        # INST Emission
        self._emit_inst_macros(table)

        return self._write_output(output_path)

    def _write_output(self, output_path: str) -> bool:
        """Write emitted output to file.

        Args:
            output_path: Path to output .h file.

        Returns:
            True if write succeeded, False otherwise.
        """
        try:
            with open(output_path, "w", encoding="utf-8") as f:
                f.write("\n".join(self.output))
            return True
        except (OSError, IOError) as e:
            self.diagnostics.error(
                ErrorCode.FILE_WRITE_ERROR,
                f"Failed to write output file: {e}"
            )
            return False

    def _emit_nodes(self, table: SymbolTable) -> None:
        """Phase 1: Emit macros for each enabled node."""
        for path, info in table.all_nodes.items():
            # Skip disabled nodes
            if info.status == NodeInfo.STATUS_DISABLED:
                continue
            # Skip special nodes
            if info.name in self._SKIP_NODES:
                continue
            self._emit_node(path, info, table)

    def _emit_node(
            self, path: str, info: NodeInfo, table: SymbolTable
    ) -> None:
        """Emit macros for a single node.

        Args:
            path: Node path in the tree.
            info: Node information from symbol table.
            table: SymbolTable for resolving references.
        """
        node_macro = self._path_to_macro(path)

        # NAME
        self._emit_section_header(path)
        self._emit_macro_exist(node_macro)
        self._emit_property_macro(node_macro, self._PROP_NAME, info.name)

        # INIT_LEVEL
        init_level = self._map_status_to_init_level(info.status)
        self._emit_property_macro(node_macro, self._PROP_INIT_LEVEL,
                                  init_level)

        # PARENT
        if info.parent:
            parent_macro = self._path_to_macro(info.parent)
            self._emit_property_macro(node_macro, self._PROP_PARENT,
                                      parent_macro)

        # ADDR
        if info.addr:
            addr_hex = info.addr if info.addr.startswith(
                "0x") else f"0x{info.addr}"
            self._emit_property_macro(node_macro, self._PROP_ADDR, addr_hex)

        # PROPERTIES
        for prop in info.properties:
            if prop.name not in self._SKIP_PROPERTIES:
                self._emit_property(node_macro, prop.name, prop.value,
                                    table)

        self.output.append("")

    def _emit_property(
            self, node_macro: str, prop_name: str, value: Value,
            table: SymbolTable
    ) -> None:
        """Emit macros for a single property.

        Args:
            node_macro: Macro name for the node.
            prop_name: Property name.
            value: Property value from AST.
            table: SymbolTable for resolving references.
        """
        prop_macro = f"{node_macro}_P_{self._normalize_identifier(prop_name)}"
        self._emit_macro_exist(prop_macro)

        if isinstance(value, ArrayValue):
            self._emit_array_macros(prop_macro, value)
        else:
            formatted = self._format_c_literal(value)
            self._emit_macro(prop_macro, formatted)

    def _emit_array_macros(
            self, base_macro: str, value: ArrayValue
    ) -> None:
        """Emit E_NUM, E_<outer_idx>_I_<inner_idx> macros for array values.

        Args:
            base_macro: Base macro name for the property.
            value: Array value containing elements.
        """
        # Check if this is a nested array (all elements are ArrayValue)
        if value.elements and all(
                isinstance(elem, ArrayValue) for elem in value.elements):
            # Nested array case: E_NUM = number of nested arrays
            self._emit_nested_array(base_macro,
                                    cast(List[ArrayValue], value.elements))
        else:
            # Flat array case: E_NUM = number of elements
            self._emit_flat_array(base_macro, value.elements)

    def _emit_nested_array(
            self, base_macro: str, nested_arrays: List[ArrayValue]
    ) -> None:
        """Emit macros for nested array structure.

        Args:
            base_macro: Base macro name for the property.
            nested_arrays: List of ArrayValue objects (nested arrays).
        """
        outer_count = len(nested_arrays)
        for outer_idx, nested_array in enumerate(nested_arrays):
            for inner_idx, elem in enumerate(nested_array.elements):
                self._emit_element(base_macro, elem, outer_idx,
                                   inner_idx)

        self._emit_macro(f"{base_macro}_E_NUM", str(outer_count))

    def _emit_flat_array(self, base_macro: str,
                         elements: List[Value]) -> None:
        """Emit macros for flat array structure.

        In a flat array, all elements belong to E_0 with different I indices.
        Phandles with following cell values are treated as single E groups.

        Args:
            base_macro: Base macro name for the property.
            elements: List of Value objects (flat array).
        """
        e_count = 0  # Count of E groups
        i_idx = 0  # I index for current E_0 group

        pos = 0
        while pos < len(elements):
            elem = elements[pos]
            if isinstance(elem, PhandleValue):
                # Phandle with following cell values - starts a new E group
                pos = self._emit_phandle_element(
                    base_macro, elem, e_count, elements, pos
                )
                e_count += 1
            else:
                # Regular elements all go into E_0
                self._emit_element(base_macro, elem, 0, i_idx)
                i_idx += 1
                pos += 1

        # For flat array, E_NUM is 1 (E_0 contains all regular elements)
        # plus number of phandle groups (each phandle group gets its own E_x)
        # If there are no phandles, E_NUM = 1
        # If there are phandles, E_NUM = 1 + phandle_count
        self._emit_macro(f"{base_macro}_E_NUM",
                         str(e_count if e_count > 0 else 1))

    def _emit_phandle_element(
            self,
            base_macro: str,
            phandle: PhandleValue,
            outer_idx: int,
            elements: List[Value],
            pos: int,
    ) -> int:
        """Emit macros for phandle element and following cell values.

        Args:
            base_macro: Base macro name for the property.
            phandle: Phandle value to emit.
            outer_idx: Element index (E_<outer_idx>).
            elements: Full list of array elements.
            pos: Current position in elements list.

        Returns:
            New position in elements list after processing.
        """
        # Phandle element (I_0)
        elem_macro = f"{base_macro}_E_{outer_idx}_I_0"
        self._emit_macro_exist(elem_macro)
        self._emit_macro(elem_macro, self._format_c_literal(phandle))

        # Cell values (I_1, I_2, ...)
        cell_idx = 1
        pos += 1
        while pos < len(elements) and isinstance(
                elements[pos], (NumberValue, FloatValue)
        ):
            elem_macro = f"{base_macro}_E_{outer_idx}_I_{cell_idx}"
            self._emit_macro_exist(elem_macro)
            self._emit_macro(elem_macro,
                             self._format_c_literal(elements[pos]))
            cell_idx += 1
            pos += 1

        return pos

    def _emit_element(
            self, base_macro: str, elem: Value, outer_idx: int,
            inner_idx: int = 0
    ) -> None:
        """Emit macros for non-phandle single element.

        Args:
            base_macro: Base macro name for the property.
            elem: Element value to emit.
            outer_idx: Element index (E_<outer_idx>).
            inner_idx: Inner index (I_<inner_idx>), defaults to 0.
        """
        elem_macro = f"{base_macro}_E_{outer_idx}_I_{inner_idx}"
        self._emit_macro_exist(elem_macro)
        self._emit_macro(elem_macro, self._format_c_literal(elem))

    def _emit_comma_separated_macros(
            self, base_macro: str, value: ArrayValue
    ) -> None:
        """Emit macros for comma-separated list: elem1, elem2, elem3.

        Supports strings, numbers, floats, identifiers, and phandles.

        Args:
            base_macro: Base macro name for the property.
            value: Array value with format="comma_separated".
        """
        # EXIST is already defined by _emit_property macro
        # Emit comma-separated list
        literals = []
        for elem in value.elements:
            literals.append(self._format_c_literal(elem))

        result = ", ".join(literals)
        self._emit_macro(base_macro, result)

    def _emit_labels_and_aliases(self, table: SymbolTable) -> None:
        """Phase 2: Emit LABEL and ALIAS macros."""
        if table.labels:
            self._emit_section_header("/LABEL")
            self._emit_symbol_macros(
                table.labels, "/LABEL"
            )

        if table.aliases:
            self._emit_section_header("/ALIAS")
            self._emit_symbol_macros(
                table.aliases, "/ALIAS"
            )

    def _emit_symbol_macros(
            self,
            symbols: Dict[str, NodeInfo],
            symbol_path: str,
    ) -> None:
        """Emit macros for labels or aliases.

        Args:
            symbols: Dictionary of label/alias names to node info.
            symbol_path: Either "LABEL" or "ALIAS".
        """
        for name, info in sorted(symbols.items()):
            macro_path = self._path_to_macro(symbol_path + '/' + name)
            macro_target = self._path_to_macro(info.path)
            self._emit_macro_exist(macro_path)
            self._emit_macro(macro_path, macro_target)

        self.output.append("")

    def _emit_inst_macros(self, table: SymbolTable) -> None:
        """Phase 3: Emit INST_S_DRIVER and INST_S_<compatible> macros."""
        enabled_nodes: List[Tuple[str, NodeInfo]] = [
            (path, info)
            for path, info in table.all_nodes.items()
            if (info.status != NodeInfo.STATUS_DISABLED and
                info.name not in self._SKIP_NODES)
        ]

        # DRIVER
        self._emit_driver_inst(enabled_nodes)

        # COMPAT
        self._emit_compatible_inst(enabled_nodes)

    def _emit_driver_inst(
            self, enabled_nodes: List[Tuple[str, NodeInfo]]
    ) -> None:
        """Emit INST_S_DRIVER macros.

        Args:
            enabled_nodes: List of (path, info) tuples for enabled nodes.
        """
        nodes = [info for _, info in enabled_nodes]
        self._emit_inst_group("/INST/DRIVER", nodes)

    def _emit_compatible_inst(
            self, enabled_nodes: List[Tuple[str, NodeInfo]]
    ) -> None:
        """Emit INST_S_<compatible> macros grouped by compatible string.

        Args:
            enabled_nodes: List of (path, info) tuples for enabled nodes.
        """
        compatible_groups: Dict[str, List[NodeInfo]] = {}

        # Group nodes by compatible
        for _, info in enabled_nodes:
            for compat in info.compatible:
                compat_key = self._normalize_identifier(compat)
                compatible_groups.setdefault(compat_key, []).append(info)

        for compat, nodes in sorted(compatible_groups.items()):
            self._emit_inst_group(f"/INST/{compat}", nodes)

    def _emit_inst_group(self, category: str, nodes: List[NodeInfo]) -> None:
        """Emit instance macros for a single category (DRIVER or compatible).

        Args:
            category: Category path like "/INST/DRIVER" or "/INST/gpio".
            nodes: List of nodes in this category.
        """
        prefix = self._path_to_macro(category)
        self._emit_section_header(category)
        self._emit_macro_exist(prefix)
        self._emit_macro(f"{prefix}_NUM", str(len(nodes)))

        for idx, info in enumerate(nodes):
            node_macro = self._path_to_macro(info.path)
            self._emit_macro(f"{prefix}_S_{idx}", node_macro)

        self.output.append("")

    def _emit_macro(self, macro: str, value: str) -> None:
        """Emit a single #define macro.

        Args:
            macro: Macro name.
            value: Macro value.
        """
        spacing = max(1, self._MACRO_DEFINE_COLUMN - len(macro))
        self.output.append(f"#define {macro}{' ' * spacing}{value}")

    def _emit_macro_exist(self, macro: str) -> None:
        """Emit a macro with EXIST flag.

        Args:
            macro: Base macro name (without _EXIST suffix).
        """
        self._emit_macro(f"{macro}_EXIST", "1")

    def _emit_property_macro(
            self, node_macro: str, prop_name: str, value: str
    ) -> None:
        """Emit a property macro with EXIST flag.

        Args:
            node_macro: Node macro name.
            prop_name: Property name.
            value: Property value.
        """
        prop_macro = f"{node_macro}_P_{prop_name}"
        self._emit_macro_exist(prop_macro)
        self._emit_macro(prop_macro, value)

    def _emit_section_header(self, content: str) -> None:
        """Emit a section header comment with total length limit.

        Args:
            content: Header content text.

        Format: /* ={fixed} {content} ={adaptive} */
        Total length limited to _SECTION_HEADER_MAX_LENGTH.
        """
        # Total: 4 (/* ) + fixed + 1 + len(content) + 1 + adaptive + 3 ( */)
        #       = 9 + fixed + len(content) + adaptive
        # Target: 80, so adaptive = 80 - 9 - fixed - len(content)
        fixed_padding = self._SECTION_HEADER_FIXED_PADDING
        adaptive_padding = max(
            1,
            self._SECTION_HEADER_MAX_LENGTH - 9 - fixed_padding - len(content)
        )
        self.output.append(
            f"/* {'=' * fixed_padding} {content} {'=' * adaptive_padding} */"
        )

    def _path_to_macro(self, path: str) -> str:
        """Convert path like /gpio/gpio0 to __MR_HT_N_S_gpio_S_gpio0.

        Args:
            path: Node path in the tree.

        Returns:
            Macro name for the path.
        """
        if path == "/":
            return f"{self._MACRO_PREFIX}N"

        # Convert path separators and normalize
        normalized_path = path.replace("/", "_S_").replace("-", "_")
        return f"{self._MACRO_PREFIX}N{normalized_path}"

    @staticmethod
    def _normalize_identifier(identifier: str) -> str:
        """Normalize identifier for macro use.

        Args:
            identifier: Original identifier string.

        Returns:
            Normalized identifier safe for macro names.
        """
        return identifier.replace("-", "_").replace(",", "_")

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

    def _map_status_to_init_level(self, status: str) -> str:
        """Map status string to INIT_LEVEL macro.

        Args:
            status: Node status string.

        Returns:
            Corresponding INIT_LEVEL macro name.
        """
        if status.startswith("prio") and len(status) > 6:
            try:
                n = int(status[5:-1])
                return f"MR_DRIVER_LEVEL_PRIO({n})"
            except ValueError:
                pass

        return self._STATUS_TO_INIT_LEVEL.get(status, "MR_DRIVER_LEVEL_NORMAL")

    def _format_c_literal(self, value: Value) -> str:
        """Format AST value for C macro output.

        Args:
            value: AST value node.

        Returns:
            C literal string representation.
        """
        if isinstance(value, StringValue):
            # Empty string represents empty property, defined as 1
            if value.value == "":
                return "1"
            escaped = value.value.replace('"', '\\"')
            return f'"{escaped}"'
        elif isinstance(value, NumberValue):
            return value.original or str(value.value)
        elif isinstance(value, FloatValue):
            return value.original or str(value.value)
        elif isinstance(value, IdentifierValue):
            return value.name
        elif isinstance(value, PhandleValue):
            if value.is_path:
                # Path reference: &{/path/to/node} - directly convert to macro
                return self._path_to_macro(value.reference)
            else:
                # Label or path reference: look up in unified map
                if self.symbol_table is None:
                    return f"/* UNRESOLVED: {value.reference} */"
                target_info = self._lookup_by_ref(
                    value.reference, self.symbol_table
                )
                if target_info:
                    return self._path_to_macro(target_info.path)
                return f"/* UNRESOLVED: {value.reference} */"
        elif isinstance(value, ArrayValue):
            return f"/* array with {len(value.elements)} elements */"
        return str(value)
