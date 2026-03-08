#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC AST - Abstract Syntax Tree definitions for Hardware Tree Compiler.

Defines node types for representing parsed hardware tree structures.
All nodes form a tree structure that can be traversed directly.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from dataclasses import dataclass, field
from typing import Optional, List


@dataclass
class SourceLocation:
    """
    Source code location for error reporting.

    Attributes:
        file_path: Path to source file.
        line: Line number (1-based).
        column: Column number (1-based).
    """

    file_path: str
    line: int
    column: int

    def __str__(self) -> str:
        if self.file_path:
            return f"{self.file_path}:{self.line}:{self.column}"
        return f"{self.line}:{self.column}"


@dataclass
class ASTNode:
    """
    Unified AST node for all nodes in the hardware tree.

    This class represents:
    - Root node (name = "/")
    - Regular nodes (e.g., "gpio0", "i2c")
    - Nodes with addresses (e.g., "aht10@38" - address stored in address field)

    Attributes:
        name: Node name ("/" for root, otherwise identifier).
        label: Optional label name from "label:" syntax.
        address: Optional address from "@address" syntax.
        properties: List of property nodes.
        children: List of child nodes.
        location: Source location.
    """

    name: str
    label: Optional[str] = None
    address: Optional[str] = None
    properties: List["PropertyNode"] = field(default_factory=list)
    children: List["ASTNode"] = field(default_factory=list)
    location: Optional[SourceLocation] = None

    @property
    def is_root(self) -> bool:
        """Check if this is the root node."""
        return self.name == "/"

    def get_property(self, name: str) -> Optional["PropertyNode"]:
        """
        Get a property by name.

        Args:
            name: Property name.

        Returns:
            PropertyNode if found, None otherwise.
        """
        return next((p for p in self.properties if p.name == name), None)


class Value:
    """Base class for property values."""
    pass


@dataclass
class StringValue(Value):
    """
    String literal value.

    Attributes:
        value: String value (without quotes).
    """

    value: str


@dataclass
class NumberValue(Value):
    """
    Integer number value.

    Supports decimal and hexadecimal formats.

    Attributes:
        value: Integer value.
        original: Original string representation.
    """

    value: int
    original: str = ""


@dataclass
class FloatValue(Value):
    """
    Floating point number value.

    Attributes:
        value: Float value.
        original: Original string representation.
    """

    value: float
    original: str = ""


@dataclass
class PhandleValue(Value):
    """
    Phandle reference value.

    Represents &label or &{/path/to/node} reference.

    Attributes:
        reference: Referenced label name or node path.
        is_path: True if this is a path reference (&{/path}), False for label (&label).
    """

    reference: str
    is_path: bool = False


@dataclass
class IdentifierValue(Value):
    """
    Identifier value (constant or macro reference).

    Represents bare identifiers used as values, e.g., MR_I2C_OPS_MODE_POLL.

    Attributes:
        name: Identifier name.
    """

    name: str


@dataclass
class ArrayValue(Value):
    """
    Array value containing multiple elements.

    Represents < ... > syntax with phandles and numbers, or string list syntax.

    Attributes:
        elements: List of values (PhandleValue, NumberValue, etc.).
    """

    elements: List[Value] = field(default_factory=list)


@dataclass
class PropertyNode:
    """
    Property node with name and value(s).

    Represents: name = value;

    Attributes:
        name: Property name.
        value: Property value (string, number, array, etc.).
        location: Source location.
    """

    name: str
    value: Value
    location: Optional[SourceLocation] = None
