#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Tree Validation - Semantic validation for hardware trees.

Runs rule checks that operate on the semantic IR after node registration has
completed, including alias resolution and reference validation.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..common import ArrayMode, ArrayValue, Diagnostics, ErrorCode, \
    ReferenceValue, SourceSpan, ValueExpr, NumberValue, FloatValue
from ..semantic import HardTree


class TreeValidator:
    """Validates a hardware tree and fills derived symbol tables."""

    NODE_ALIASES_PATH = "/aliases"
    STATUS_DISABLED = "disabled"

    def __init__(self, diagnostics: Diagnostics) -> None:
        """Initialize the validator with a shared diagnostic sink."""
        self.diagnostics = diagnostics

    def validate(self, tree: HardTree) -> None:
        """Run all post-build semantic validation passes on a tree."""
        self._resolve_aliases(tree)
        self._verify_references(tree)
        self._verify_array_modes(tree)

    def _resolve_aliases(self, tree: HardTree) -> None:
        """Resolve alias properties into the alias symbol table."""
        # Aliases are modeled as a dedicated node layered on top of real paths
        aliases_node = tree.nodes_by_path.get(self.NODE_ALIASES_PATH)
        if aliases_node is None:
            return

        for prop in aliases_node.properties:
            # Alias values must be references
            if not isinstance(prop.value, ReferenceValue):
                self._warn_at(
                    ErrorCode.INVALID_ALIAS_VALUE,
                    f"aliases property '{prop.name}' must be a reference",
                    prop.span,
                )
                continue

            # Alias targets reuse the same reference lookup rules as properties
            target_path = self._lookup_reference(prop.value, tree)
            if target_path is None:
                self._error_at(ErrorCode.UNDEFINED_LABEL,
                               f"undefined reference: &{prop.value.reference}",
                               prop.span)
                continue

            # Alias names must be unique
            if prop.name in tree.aliases:
                self._error_at(ErrorCode.DUPLICATE_ALIAS,
                               f"duplicate alias: {prop.name}", prop.span)
                continue

            # Add the alias to the alias symbol table
            tree.aliases[prop.name] = target_path

    def _verify_references(self, tree: HardTree) -> None:
        """Verify that all references resolve to existing semantic nodes."""
        for node in tree.nodes:
            for prop in node.properties:
                for reference in self._iter_references(prop.value):
                    # References may resolve through a label or a canonical path
                    target_path = self._lookup_reference(reference, tree)
                    if target_path is None:
                        self._error_at(ErrorCode.UNDEFINED_LABEL,
                                       f"undefined reference: &{reference.reference}",
                                       reference.span)
                        continue

                    # Disabled targets are still resolvable but worth warning on
                    target = tree.nodes_by_path[target_path]
                    if target.effective_status == self.STATUS_DISABLED:
                        self._warn_at(
                            ErrorCode.REFERENCE_TO_DISABLED,
                            f"reference points to disabled node: {target_path}",
                            reference.span,
                        )

    def _verify_array_modes(self, tree: HardTree) -> None:
        """Verify allowed nesting combinations for array render modes."""
        for node in tree.nodes:
            for prop in node.properties:
                self._check_array_modes(prop.value, prop.name, node.path)

    @staticmethod
    def _lookup_reference(reference: ReferenceValue,
                          tree: HardTree) -> str | None:
        """Resolve a reference value to its canonical semantic path."""
        # Path references are already canonical
        if reference.is_path:
            return reference.reference if reference.reference in tree.nodes_by_path else None

        # Label references take priority over shorthand node-name lookup
        label_target = tree.labels.get(reference.reference)
        if label_target is not None:
            return label_target

        # Shorthand `&uart0` resolves to `/uart0` when it exists
        path = f"/{reference.reference}"
        return path if path in tree.nodes_by_path else None

    def _iter_references(self, value: ValueExpr):
        """Yield nested reference values from a value expression tree."""
        if isinstance(value, ReferenceValue):
            yield value
            return
        if isinstance(value, ArrayValue):
            for element in value.elements:
                yield from self._iter_references(element)

    def _check_array_modes(self, value: ValueExpr, prop_name: str,
                           node_path: str, inline_parent: bool = False) -> None:
        """Validate that inline arrays contain only numeric scalar elements."""
        # Inline arrays may contain only integers, floats, or nested inline arrays
        if inline_parent and not isinstance(value,
                                            (ArrayValue, NumberValue,
                                             FloatValue)):
            self._error_at(
                ErrorCode.INVALID_INLINE_ARRAY,
                f"property '{prop_name}' in '{node_path}' can only contain numbers or floats inside inline arrays",
                value.span,
            )
            return
        if not isinstance(value, ArrayValue):
            return

        # Inline arrays cannot contain expanded arrays
        if inline_parent and value.mode == ArrayMode.EXPANDED:
            self._error_at(
                ErrorCode.INVALID_INLINE_ARRAY,
                f"property '{prop_name}' in '{node_path}' cannot nest expanded arrays inside inline arrays",
                value.span,
            )
            return

        # Expanded arrays may contain inline arrays
        next_inline_parent = value.mode == ArrayMode.INLINE
        for element in value.elements:
            self._check_array_modes(element, prop_name, node_path,
                                    inline_parent=next_inline_parent)

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
