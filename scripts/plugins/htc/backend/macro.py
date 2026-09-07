#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Backend Macro - Header macro lowering for Hardware Tree Compiler.

Transforms semantic hardware tree data into static C preprocessor macro
definitions used by embedded targets.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..common import (
    ArrayMode,
    ArrayValue,
    ByteStringValue,
    EmptyValue,
    FloatValue,
    IdentifierValue,
    NumberValue,
    ReferenceValue,
    STATUS_DEFER,
    STATUS_DISABLED,
    STATUS_EARLY,
    STATUS_LAST,
    STATUS_OKAY,
    STATUS_PRIO,
    STATUS_PRIO_PREFIX,
    STATUS_PRIO_SUFFIX,
    StringValue,
    ValueExpr,
)
from ..semantic import HardNode, HardTree, ReferenceResolver
from .names import MacroNames


class MacroBuilder:
    """Lowers a semantic hardware tree into backend macro lines."""

    SECTION_LABEL = "LABEL"
    SECTION_ALIAS = "ALIAS"
    SECTION_INST = "INST"
    SECTION_DRIVER = "DRIVER"
    PROPERTY_NAME = "NAME"
    PROPERTY_PATH = "PATH"
    PROPERTY_INIT_LEVEL = "INIT_LEVEL"
    PROPERTY_PARENT = "PARENT"
    PROPERTY_ADDR = "ADDR"
    STATUS_EARLY = STATUS_EARLY
    STATUS_OKAY = STATUS_OKAY
    STATUS_LAST = STATUS_LAST
    STATUS_DEFER = STATUS_DEFER
    STATUS_DISABLED = STATUS_DISABLED
    STATUS_PRIO_PREFIX = STATUS_PRIO_PREFIX
    STATUS_PRIO_SUFFIX = STATUS_PRIO_SUFFIX
    STATUS_PRIO = STATUS_PRIO
    STATUS_TO_INIT_LEVEL = {
        STATUS_EARLY: "MR_DRIVER_LEVEL_EARLY",
        STATUS_OKAY: "MR_DRIVER_LEVEL_NORMAL",
        STATUS_LAST: "MR_DRIVER_LEVEL_LAST",
        STATUS_DEFER: "MR_DRIVER_LEVEL_DEFER",
        STATUS_DISABLED: "MR_DRIVER_LEVEL_DEFER",
        STATUS_PRIO: "MR_DRIVER_LEVEL_PRIO"
    }
    DEFAULT_ARRAY_VALUE = "0"
    DEFAULT_FLAG_VALUE = "1"
    DEFAULT_INIT_LEVEL = "MR_DRIVER_LEVEL_NORMAL"

    def build(self, tree: HardTree) -> list[str]:
        """Build all backend macro lines for a semantic hardware tree."""
        lines: list[str] = []

        # Skip disabled nodes and synthetic helper nodes
        emitted_nodes = self._iter_emitted_nodes(tree)
        for node in emitted_nodes:
            # Emit all node-local macros first
            self._emit_node(lines, tree, node)

        # Emit global symbol tables next, but only for emitted targets.
        emitted_paths = {node.path for node in emitted_nodes}
        self._emit_symbols(lines, tree.labels, self.SECTION_LABEL,
                           emitted_paths)
        self._emit_symbols(lines, tree.aliases, self.SECTION_ALIAS,
                           emitted_paths)

        # Only compatible nodes participate in driver instance indexes
        instance_nodes = [node for node in emitted_nodes if node.compatible]
        self._emit_inst(lines, instance_nodes)
        return lines

    def _iter_emitted_nodes(self, tree: HardTree) -> list[HardNode]:
        """Return enabled nodes that should receive node-local macros."""
        return [node for node in tree.nodes if self._should_emit_node(node)]

    def _emit_expanded_array(self, lines: list[str], tree: HardTree,
                             base: str, value: ArrayValue) -> None:
        """Emit grouped array macros following HTC backend conventions."""
        # Nested expanded arrays: each sub-array forms one E group
        # e.g. <<a b c> <d e f>> → E_0={a,b,c}, E_1={d,e,f}, E_NUM=2
        if value.elements and all(
                isinstance(e, ArrayValue) and e.mode == ArrayMode.EXPANDED
                for e in value.elements):
            for outer_index, sub in enumerate(value.elements):
                for inner_index, element in enumerate(sub.elements):
                    self._emit_array_element(lines, tree, base,
                                             outer_index, inner_index, element)
            self._define(lines, f"{base}_E_NUM", str(len(value.elements)))
            return
        # Flat expanded array: all elements form a single E_0 group
        # e.g. <&gpio0 0> → E_0_I_0=&gpio0, E_0_I_1=0, E_NUM=1
        for inner_index, element in enumerate(value.elements):
            self._emit_array_element(lines, tree, base, 0, inner_index, element)
        self._define(lines, f"{base}_E_NUM", "1")

    def _emit_array_element(
            self,
            lines: list[str],
            tree: HardTree,
            base: str,
            outer_index: int,
            inner_index: int,
            value: ValueExpr,
    ) -> None:
        """Emit one concrete array element macro."""
        element_macro = f"{base}_E_{outer_index}_I_{inner_index}"
        self._define_exist(lines, element_macro)

        # array-mode-rule: expanded arrays may embed inline arrays as values
        self._define(lines, element_macro,
                     self._format_expanded_element(tree, value))

    def _emit_symbols(self, lines: list[str], symbols: dict[str, str],
                      section: str, emitted_paths: set[str]) -> None:
        """Emit label or alias symbol tables as backend macros."""
        symbols = {
            name: target_path for name, target_path in symbols.items()
            if target_path in emitted_paths
        }
        if not symbols:
            return
        lines.append(self._section_header(f"/{section}"))
        for name, target_path in sorted(symbols.items()):
            macro = self._path_to_macro(f"/{section}/{name}")
            self._define_exist(lines, macro)
            self._define(lines, macro, self._path_to_macro(target_path))
        lines.append("")

    def _emit_inst(self, lines: list[str],
                   instance_nodes: list[HardNode]) -> None:
        """Emit indexes for enabled nodes that declare compatible values."""
        self._emit_inst_group(lines,
                              f"/{self.SECTION_INST}/{self.SECTION_DRIVER}",
                              instance_nodes)
        groups: dict[str, list[HardNode]] = {}
        for node in instance_nodes:
            for compatible in node.compatible:
                groups.setdefault(self._normalize(compatible), []).append(node)
        for compatible, nodes in sorted(groups.items()):
            self._emit_inst_group(lines, f"/{self.SECTION_INST}/{compatible}",
                                  nodes)

    def _emit_inst_group(self, lines: list[str], category: str,
                         nodes: list[HardNode]) -> None:
        """Emit one instance group category and its indexed members."""
        prefix = self._path_to_macro(category)
        lines.append(self._section_header(category))
        self._define_exist(lines, prefix)
        self._define(lines, f"{prefix}_NUM", str(len(nodes)))
        for index, node in enumerate(nodes):
            self._define(lines, f"{prefix}_S_{index}",
                         self._path_to_macro(node.path))
        lines.append("")

    def _format_inline_array(self, tree: HardTree,
                             value: ArrayValue) -> str:
        """Render an inline array as a C brace-enclosed initializer."""
        rendered = [self._format_inline_element(tree, element)
                    for element in value.elements]
        return "{" + ", ".join(rendered) + "}"

    def _format_inline_element(self, tree: HardTree,
                               value: ValueExpr) -> str:
        """Render one inline-array element, recursing into nested arrays."""
        if isinstance(value, ArrayValue):
            # Inline arrays only recurse into inline arrays
            if value.mode == ArrayMode.EXPANDED:
                raise ValueError(
                    "expanded arrays cannot appear inside inline arrays")
            return self._format_inline_array(tree, value)
        return self._format_value(tree, value)

    def _format_expanded_element(self, tree: HardTree,
                                 value: ValueExpr) -> str:
        """Render one expanded-array element, allowing inline array literals."""
        if isinstance(value, ArrayValue) and value.mode == ArrayMode.INLINE:
            # array-mode-rule: expanded arrays may inline brace initializers
            return self._format_inline_array(tree, value)
        return self._format_value(tree, value)

    def _map_status(self, status: str) -> str:
        """Map semantic status strings to backend init-level macros."""
        if status.startswith(self.STATUS_PRIO_PREFIX) and status.endswith(
                self.STATUS_PRIO_SUFFIX):
            start = len(self.STATUS_PRIO_PREFIX)
            end = -len(self.STATUS_PRIO_SUFFIX)
            macro = self.STATUS_TO_INIT_LEVEL[self.STATUS_PRIO]
            return f"{macro}({status[start:end]})"
        return self.STATUS_TO_INIT_LEVEL.get(status, self.DEFAULT_INIT_LEVEL)

    @staticmethod
    def _section_header(title: str) -> str:
        """Build a banner comment for one logical output section."""
        fixed = 32
        adaptive = max(1, 80 - 9 - fixed - len(title))
        return f"/* {'=' * fixed} {title} {'=' * adaptive} */"

    @staticmethod
    def _escape_c_string(value: str) -> str:
        """Escape a Python string value for a C string literal."""
        escapes = {
            "\\": "\\\\",
            '"': '\\"',
            "\n": "\\n",
            "\r": "\\r",
            "\t": "\\t",
        }
        return "".join(escapes.get(char, char) for char in value)

    @staticmethod
    def render(lines: list[str]) -> str:
        """Join rendered lines into a newline-terminated header string."""
        return "\n".join(lines).rstrip() + "\n"

    @staticmethod
    def _define(lines: list[str], macro: str, value: str) -> None:
        """Append one aligned `#define` line"""
        spacing = max(1, 50 - len(macro))
        lines.append(f"#define {macro}{' ' * spacing}{value}")

    def _define_exist(self, lines: list[str], macro: str) -> None:
        """Append the paired existence macro for a symbol."""
        self._define(lines, f"{macro}_EXIST", "1")

    def _define_property(self, lines: list[str], node_macro: str,
                         prop_name: str, value: str) -> None:
        """Emit a property existence macro and its value macro."""
        prop_macro = f"{node_macro}_P_{prop_name}"
        self._define_exist(lines, prop_macro)
        self._define(lines, prop_macro, value)

    def _should_emit_node(self, node: HardNode) -> bool:
        """Return True when a hardware node should be emitted."""
        return (node.path != "/aliases"
                and node.effective_status != self.STATUS_DISABLED)

    def _path_to_macro(self, path: str) -> str:
        """Convert a canonical DTS path into an HTC macro prefix."""
        return MacroNames.node_macro(path)

    @staticmethod
    def _normalize(identifier: str) -> str:
        """Normalize user-facing names into macro-safe fragments."""
        return MacroNames.normalize_identifier(identifier)

    def _emit_property(self, lines: list[str], tree: HardTree,
                       node_macro: str, prop_name: str,
                       value: ValueExpr) -> None:
        """Emit macros for one node property value."""
        base = f"{node_macro}_P_{self._normalize(prop_name)}"
        self._define_exist(lines, base)

        # DTS bytestring: aggregate plus indexed bytes
        if isinstance(value, ByteStringValue):
            self._emit_byte_string(lines, base, value)
            return

        # Expanded arrays: grouped element macros plus a scalar length macro
        if isinstance(value, ArrayValue) and value.mode == ArrayMode.EXPANDED:
            self._emit_expanded_array(lines, tree, base, value)
            self._define(lines, f"{base}_LEN", str(len(value.elements)))
            if len(value.elements) == 1:
                self._define(lines, base,
                             self._format_expanded_element(tree,
                                                           value.elements[0]))
            return

        # Inline arrays: one aggregate macro plus a scalar length macro
        if isinstance(value, ArrayValue) and value.mode == ArrayMode.INLINE:
            self._define(lines, base, self._format_value(tree, value))
            self._define(lines, f"{base}_LEN", str(len(value.elements)))
            for index, element in enumerate(value.elements):
                element_macro = f"{base}_E_0_I_{index}"
                self._define_exist(lines, element_macro)
                self._define(lines, element_macro,
                             self._format_value(tree, element))
            return

        # Simple values
        self._define(lines, base, self._format_value(tree, value))

    def _emit_byte_string(self, lines: list[str], base: str,
                          value: ByteStringValue) -> None:
        """Emit a DTS bytestring as an aggregate and indexed byte macros."""
        rendered = ", ".join(f"0x{byte:02x}" for byte in value.values)
        self._define(lines, base, "{" + rendered + "}")
        self._define(lines, f"{base}_LEN", str(len(value.values)))
        for index, byte in enumerate(value.values):
            element_macro = f"{base}_E_0_I_{index}"
            self._define_exist(lines, element_macro)
            self._define(lines, element_macro, f"0x{byte:02x}")

    def _format_value(self, tree: HardTree, value: ValueExpr) -> str:
        """Format a semantic value expression as backend macro text."""
        # Bare properties map to flag-style macros with value 1
        if isinstance(value, EmptyValue):
            return self.DEFAULT_FLAG_VALUE

        # Strings preserve original quoting at render time
        if isinstance(value, StringValue):
            return f'"{self._escape_c_string(value.value)}"'

        # Numeric and identifier values are emitted as written
        if isinstance(value, NumberValue):
            return value.original
        if isinstance(value, FloatValue):
            return value.original
        if isinstance(value, IdentifierValue):
            return value.name

        # References are resolved through label, path, then name
        if isinstance(value, ReferenceValue):
            target = ReferenceResolver.resolve(tree, value.reference,
                                               value.is_path)
            return self._path_to_macro(target or f"/{value.reference}")

        # DTS bytestrings render as C brace-enclosed values
        if isinstance(value, ByteStringValue):
            rendered = ", ".join(f"0x{byte:02x}" for byte in value.values)
            return "{" + rendered + "}"

        # Inline arrays render as direct brace-enclosed values
        if isinstance(value, ArrayValue):
            if value.mode == ArrayMode.INLINE:
                return self._format_inline_array(tree, value)
            return self.DEFAULT_ARRAY_VALUE
        return str(value)

    @staticmethod
    def _format_address(address: str) -> str:
        """Normalize node address text into backend hex form."""
        if address is None:
            return "0"
        return ", ".join(f"0x{part}" for part in address.split(","))

    def _emit_node(self, lines: list[str], tree: HardTree,
                   node: HardNode) -> None:
        """Emit node macros, skipping DTS bus metadata properties."""
        # Section header
        node_macro = self._path_to_macro(node.path)
        lines.append(self._section_header(node.path))

        # EXIST
        self._define_exist(lines, node_macro)

        # NAME
        self._define_property(lines, node_macro, self.PROPERTY_NAME,
                              MacroNames.node_identifier(node.name,
                                                         node.address))

        # PATH
        self._define_property(lines, node_macro, self.PROPERTY_PATH,
                              f'"{node.path}"')

        # INIT_LEVEL
        self._define_property(lines, node_macro, self.PROPERTY_INIT_LEVEL,
                              self._map_status(node.effective_status))

        # PARENT
        if node.parent_path is not None:
            self._define_property(lines, node_macro, self.PROPERTY_PARENT,
                                  self._path_to_macro(node.parent_path))

        # ADDR
        if node.address is not None:
            self._define_property(lines, node_macro, self.PROPERTY_ADDR,
                                  self._format_address(node.address))

        # Properties
        for prop in node.properties:
            if prop.name.startswith("#"):
                continue
            self._emit_property(lines, tree, node_macro, prop.name, prop.value)
        lines.append("")
