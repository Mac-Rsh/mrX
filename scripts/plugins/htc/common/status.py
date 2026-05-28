#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Status - Shared node status constants and helpers.

Keeps semantic validation and backend emission aligned on status names.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

STATUS_OKAY = "okay"
STATUS_DISABLED = "disabled"
STATUS_EARLY = "early"
STATUS_LAST = "last"
STATUS_DEFER = "defer"
STATUS_PRIO_PREFIX = "prio("
STATUS_PRIO_SUFFIX = ")"
STATUS_PRIO = "prio"

VALID_STATUS_VALUES = {
    STATUS_OKAY,
    STATUS_DISABLED,
    STATUS_EARLY,
    STATUS_LAST,
    STATUS_DEFER,
}


def parse_priority_status(status: str) -> int | None:
    """Return the priority value from `prio(n)`, or None when not priority."""
    if not status.startswith(STATUS_PRIO_PREFIX):
        return None
    if not status.endswith(STATUS_PRIO_SUFFIX):
        return None
    return int(status[len(STATUS_PRIO_PREFIX):-len(STATUS_PRIO_SUFFIX)])
