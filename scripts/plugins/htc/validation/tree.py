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
    ReferenceValue, SourceSpan, STATUS_DISABLED, StringValue, ValueExpr, \
    NumberValue, FloatValue
from ..semantic import HardTree, ReferenceResolver
from ..backend.names import MacroNames


class TreeValidator:
    """Validates a hardware tree and fills derived symbol tables."""

    NODE_ALIASES_PATH = "/aliases"
    STATUS_DISABLED = STATUS_DISABLED

    def __init__(self, diagnostics: Diagnostics) -> None:
        """Initialize the validator with a shared diagnostic sink."""
        self.diagnostics = diagnostics

    def validate(self, tree: HardTree) -> None:
        """Run all post-build semantic validation passes on a tree."""
        self._resolve_aliases(tree)
        self._verify_macro_name_collisions(tree)
        self._verify_references(tree)
        self._verify_array_modes(tree)

    def _resolve_aliases(self, tree: HardTree) -> None:
        """Resolve alias properties into the alias symbol table."""
        # Aliases are modeled as a dedicated node layered on top of real paths
        aliases_node = tree.nodes_by_path.get(self.NODE_ALIASES_PATH)
        if aliases_node is None:
            return

        for prop in aliases_node.properties:
            # Alias values may be DTS path strings or HTC label/path references.
            if isinstance(prop.value, StringValue):
                target_path = self._resolve_string_path(
                    prop.value.value, tree)
                if target_path is None:
                    self._error_at(
                        ErrorCode.UNDEFINED_LABEL,
                        f"undefined alias path: '{prop.value.value}'",
                        prop.value.span or prop.span,
                    )
                    continue
            elif isinstance(prop.value, ReferenceValue):
                target_path, ambiguous = self._lookup_reference_checked(
                    prop.value, tree)
                if target_path is None:
                    if not ambiguous:
                        self._error_at(
                            ErrorCode.UNDEFINED_LABEL,
                            f"undefined reference: &{prop.value.reference}",
                            prop.value.span or prop.span,
                        )
                    continue
            else:
                self._warn_at(
                    ErrorCode.INVALID_ALIAS_VALUE,
                    f"aliases property '{prop.name}' must be a path string or "
                    "a reference",
                    prop.value.span or prop.span,
                )
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
            if node.path == self.NODE_ALIASES_PATH:
                continue
            for prop in node.properties:
                for reference in self._iter_references(prop.value):
                    # References may resolve through a label or a canonical path
                    target_path, ambiguous = self._lookup_reference_checked(
                        reference, tree)
                    if target_path is None:
                        if not ambiguous:
                            self._error_at(
                                ErrorCode.UNDEFINED_LABEL,
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

    def _verify_macro_name_collisions(self, tree: HardTree) -> None:
        """Report distinct DTS names that collide after macro normalization."""
        self._verify_node_name_collisions(tree)
        self._verify_label_name_collisions(tree)
        self._verify_property_name_collisions(tree)
        self._verify_compatible_name_collisions(tree)

    def _verify_node_name_collisions(self, tree: HardTree) -> None:
        """Report node paths that normalize to the same backend macro name."""
        seen: dict[str, str] = {}
        for path, node in tree.nodes_by_path.items():
            macro_path = MacroNames.normalize_path(path)
            existing = seen.get(macro_path)
            if existing is not None and existing != path:
                self._error_at(
                    ErrorCode.DUPLICATE_NODE,
                    f"node paths '{existing}' and '{path}' both normalize to "
                    f"macro path '{macro_path}'",
                    node.span,
                )
                continue
            seen[macro_path] = path

    def _verify_label_name_collisions(self, tree: HardTree) -> None:
        """Report labels that normalize to the same backend label macro."""
        seen: dict[str, str] = {}
        for label, path in tree.labels.items():
            macro_label = MacroNames.normalize_identifier(label)
            existing = seen.get(macro_label)
            if existing is not None and existing != label:
                node = tree.nodes_by_path.get(path)
                self._error_at(
                    ErrorCode.DUPLICATE_LABEL,
                    f"labels '{existing}' and '{label}' both normalize to "
                    f"macro name '{macro_label}'",
                    node.span if node is not None else None,
                )
                continue
            seen[macro_label] = label

    def _verify_property_name_collisions(self, tree: HardTree) -> None:
        """Report properties in one node that normalize to the same macro."""
        for path, node in tree.nodes_by_path.items():
            seen: dict[str, str] = {}
            for prop in node.properties:
                if prop.name.startswith("#"):
                    continue
                macro_prop = MacroNames.normalize_identifier(prop.name)
                existing = seen.get(macro_prop)
                if existing is not None and existing != prop.name:
                    self._error_at(
                        ErrorCode.INVALID_PROPERTY,
                        f"properties '{existing}' and '{prop.name}' in node "
                        f"'{path}' both normalize to macro name '{macro_prop}'",
                        prop.span,
                    )
                    continue
                seen[macro_prop] = prop.name

    def _verify_compatible_name_collisions(self, tree: HardTree) -> None:
        """Warn when two compatible strings normalize to the same macro."""
        seen: dict[str, str] = {}
        for node in tree.nodes:
            for compatible in node.compatible:
                macro_name = MacroNames.normalize_identifier(compatible)
                existing = seen.get(macro_name)
                if existing is not None and existing != compatible:
                    self._warn_at(
                        ErrorCode.INVALID_COMPATIBLE_VALUE,
                        f"compatible strings '{existing}' and '{compatible}' "
                        f"both normalize to macro name '{macro_name}'; "
                        "prefer the comma form",
                        node.span,
                    )
                    continue
                seen[macro_name] = compatible

    def _lookup_reference(self, reference: ReferenceValue,
                          tree: HardTree) -> str | None:
        """Resolve a reference value to its canonical semantic path."""
        return ReferenceResolver.resolve(tree, reference.reference,
                                         reference.is_path)

    def _lookup_reference_checked(self, reference: ReferenceValue,
                                  tree: HardTree) -> tuple[str | None, bool]:
        """Resolve a reference and report ambiguous name-only lookups.

        Returns ``(target_path, ambiguous)``.  When ``ambiguous`` is true, an
        error has already been emitted and the caller should not emit a second
        undefined-reference error.
        """
        if not reference.is_path:
            label_target = tree.labels.get(reference.reference)
            short_path = f"/{reference.reference}"
            if label_target is None and short_path not in tree.nodes_by_path:
                candidates = ReferenceResolver.find_by_name(
                    tree, reference.reference)
                if len(candidates) > 1:
                    self._error_at(
                        ErrorCode.UNDEFINED_LABEL,
                        f"ambiguous reference: &{reference.reference} matches "
                        f"nodes {', '.join(candidates)}; use a label or "
                        "&{/full/path} instead",
                        reference.span,
                    )
                    return None, True
        return self._lookup_reference(reference, tree), False

    @staticmethod
    def _resolve_string_path(path: str, tree: HardTree) -> str | None:
        """Resolve a DTS alias path string to a canonical node path."""
        if not path.startswith("/"):
            path = f"/{path}"
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
