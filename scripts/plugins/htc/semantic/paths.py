#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Semantic Paths - Canonical DTS path construction helpers.

Centralizes unit-address normalization and parent/child path composition for
semantic building, modification, deletion, and reference resolution.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""


def normalize_unit_address(address: str | None) -> str | None:
    """Normalize a comma-separated DTS unit address to lowercase."""
    if address is None:
        return None
    return ",".join(part.lower() for part in address.split(","))


def node_segment(name: str, address: str | None) -> str:
    """Build one canonical node path segment."""
    normalized = normalize_unit_address(address)
    return name if normalized is None else f"{name}@{normalized}"


def child_path(parent_path: str | None, segment: str) -> str:
    """Join a parent path and one canonical child segment."""
    if parent_path in (None, "/"):
        return f"/{segment}"
    return f"{parent_path}/{segment}"


def normalize_node_path(path: str) -> str:
    """Normalize every unit address embedded in a node path."""
    segments = []
    for segment in path.split("/"):
        if "@" not in segment:
            segments.append(segment)
            continue
        name, address = segment.split("@", 1)
        segments.append(node_segment(name, address))
    return "/".join(segments)
