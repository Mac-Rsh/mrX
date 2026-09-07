#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
First-party plugin contract and catalog loading.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING
import importlib

from .diagnostics import Diagnostic, DiagnosticLevel
from .command import Command

if TYPE_CHECKING:
    from .console import Console

_PLUGIN_FACTORY = "create_plugin"


@dataclass(frozen=True, slots=True)
class ApplicationServices:
    """Services available to plugin factories during application startup."""

    project_root: Path
    environment: Mapping[str, str]
    console: "Console"


@dataclass(frozen=True, slots=True)
class Plugin:
    """One first-party plugin and the commands it contributes."""

    name: str
    version: str
    description: str
    commands: tuple[Command, ...]


@dataclass(frozen=True, slots=True)
class CatalogLoadResult:
    """Plugins and diagnostics collected while loading the catalog."""

    plugins: tuple[Plugin, ...]
    diagnostics: tuple[Diagnostic, ...]


def load_plugin_catalog(
        services: ApplicationServices,
        module_names: Sequence[str],
) -> CatalogLoadResult:
    """Load first-party plugins from an explicit module catalog.

    A single plugin failure is recorded and skipped so remaining plugins
    and built-in commands can still run.

    Arguments:
        services: Construction-time services passed to each factory.
        module_names: Import paths listed in the plugin catalog.

    Returns:
        Successfully constructed plugins and load diagnostics.
    """
    plugins: list[Plugin] = []
    diagnostics: list[Diagnostic] = []

    for module_name in module_names:
        try:
            plugin = _load_one_plugin(services, module_name)
        except Exception as exc:
            diagnostics.append(_load_diagnostic(module_name, exc))
            continue
        plugins.append(plugin)

    return CatalogLoadResult(
        plugins=tuple(plugins),
        diagnostics=tuple(diagnostics),
    )


def _load_one_plugin(services: ApplicationServices, module_name: str) -> Plugin:
    """Import one catalog entry and validate its factory return value."""
    module = importlib.import_module(module_name)
    factory = getattr(module, _PLUGIN_FACTORY, None)
    if factory is None:
        raise TypeError(
            f"plugin entry '{module_name}' has no {_PLUGIN_FACTORY}()")
    if not callable(factory):
        raise TypeError(f"{module_name}.{_PLUGIN_FACTORY} is not callable")

    plugin = factory(services)
    if not isinstance(plugin, Plugin):
        raise TypeError(
            f"{module_name}.{_PLUGIN_FACTORY}() must return Plugin, "
            f"got {type(plugin).__name__}"
        )
    if not isinstance(plugin.commands, tuple):
        raise TypeError(
            f"Plugin '{plugin.name}' commands must be a tuple of Command"
        )
    for command in plugin.commands:
        if not isinstance(command, Command):
            raise TypeError(
                f"Plugin '{plugin.name}' returned a non-Command value: "
                f"{command!r}"
            )
    return plugin


def _load_diagnostic(source: str, error: Exception) -> Diagnostic:
    """Build one actionable plugin loading diagnostic."""
    return Diagnostic(
        level=DiagnosticLevel.ERROR,
        code="PLUGIN_LOAD_FAILED",
        message=f"Failed to load plugin '{source}': {error}",
        hint=f"Expose {_PLUGIN_FACTORY}(services) from the plugin package",
    )
