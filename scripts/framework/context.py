#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Execution context shared with command implementations.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from collections.abc import Mapping
from dataclasses import dataclass, field
from pathlib import Path
import os
import re

from .console import Console

_VERSION_MACRO = re.compile(
    r"#define\s+MR_VERSION_(MAJOR|MINOR|PATCH)\s+\((\d+)U\)"
)


@dataclass(slots=True)
class CancellationToken:
    """Mutable cooperative cancellation state for long-running commands."""

    cancelled: bool = False

    def cancel(self) -> None:
        """Request cancellation of the current command."""
        self.cancelled = True

    def raise_if_cancelled(self) -> None:
        """Raise an interruption when cancellation was requested."""
        if self.cancelled:
            raise KeyboardInterrupt


@dataclass(slots=True)
class SessionState:
    """Mutable interactive session state shared across command invocations."""

    cwd: Path
    last_exit_code: int = 0
    exit_requested: bool = False


@dataclass(slots=True)
class CommandContext:
    """Runtime services and process state available to one command."""

    project_root: Path
    session: SessionState
    environment: Mapping[str, str]
    console: Console
    verbose: bool = False
    interactive: bool = False
    cancellation: CancellationToken = field(default_factory=CancellationToken)

    @property
    def cwd(self) -> Path:
        """Return the current session working directory."""
        return self.session.cwd

    def resolve_path(self, value: str | Path, *,
                     project: bool = False) -> Path:
        """Resolve one path against the session cwd or the project root.

        User-supplied relative paths use the session cwd. Project defaults
        such as Kconfig, `.config`, and generated headers use `project_root`
        when `project` is true.

        Arguments:
            value: Path string or path object supplied by the caller.
            project: Resolve relative paths against the project root.

        Returns:
            Absolute resolved path.
        """
        path = Path(value)
        if path.is_absolute():
            return path.resolve()
        root = self.project_root if project else self.session.cwd
        return (root / path).resolve()

    def resolve_optional_path(
            self,
            value: str | None,
            default: str | Path,
    ) -> Path:
        """Resolve an optional user path, falling back to a project default.

        Arguments:
            value: User-supplied path, or `None` when the default applies.
            default: Project-relative default path.

        Returns:
            Absolute resolved path.
        """
        if value is None:
            return self.resolve_path(default, project=True)
        return self.resolve_path(value)


def detect_project_root() -> Path:
    """Locate the mrX repository root that contains `scripts/cli.py`."""
    current = Path(__file__).resolve().parent
    for candidate in (current, *current.parents):
        if (candidate / "scripts" / "cli.py").is_file() and (
                candidate / "Kconfig").is_file():
            return candidate
    return Path(__file__).resolve().parent.parent.parent


def read_project_version(project_root: Path) -> str:
    """Read the mrX version from the kernel version header."""
    header = project_root / "include" / "mrX" / "kernel" / "mr_version.h"
    try:
        text = header.read_text(encoding="utf-8")
    except OSError:
        return "0.0.0"

    parts: dict[str, str] = {}
    for match in _VERSION_MACRO.finditer(text):
        parts[match.group(1).lower()] = match.group(2)
    if {"major", "minor", "patch"} <= parts.keys():
        return f"{parts['major']}.{parts['minor']}.{parts['patch']}"
    return "0.0.0"


def current_environment() -> dict[str, str]:
    """Return a copy of the process environment."""
    return dict(os.environ)
