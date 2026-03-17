#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Plugin dock - Plugin discovery and registration management.

Loads exported plugins from the `plugins` package, validates required plugin
interfaces, and builds a command lookup table with alias support.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from typing import Protocol, TypeVar, cast
from dataclasses import dataclass
import plugins as plugins_dir
import importlib
import inspect
import pkgutil

_ExportPlugin = TypeVar("_ExportPlugin", bound="Plugin")
_EXPORT_ATTR = "__export_plugin__"


@dataclass(slots=True, frozen=True)
class PluginSpec:
    """Metadata definition for a dock plugin."""

    name: str
    description: str
    author: str = ""
    version: str = ""
    aliases: tuple[str, ...] = ()


class Plugin(Protocol):
    """Runtime contract that every exported plugin must satisfy."""

    spec: PluginSpec

    def help(self) -> str:
        """Return command help text.

        This hook is optional at runtime. Commands may omit it and rely on the
        dock's default help formatter instead.

        Returns:
            Human-readable help text for the command.
        """

    def run(self, args: list[str]) -> int:
        """Execute plugin logic.

        Args:
            args: Command line arguments passed to the plugin.

        Returns:
            Exit code (0 for success, non-zero for error).
        """


def export_plugin(plugin: _ExportPlugin) -> _ExportPlugin:
    """Mark a plugin class or instance as discoverable.

    Args:
        plugin: Plugin implementation to expose through the dock.

    Returns:
        The same plugin object for decorator-style usage.
    """
    setattr(plugin, _EXPORT_ATTR, True)
    return plugin


def is_exported(plugin: object) -> bool:
    """Check whether a plugin object is marked for export.

    Args:
        plugin: Candidate plugin class or instance.

    Returns:
        `True` when the export marker is present, otherwise `False`.
    """
    return bool(getattr(plugin, _EXPORT_ATTR, False))


class Dock:
    """Plugin registry builder for the command dock."""

    @classmethod
    def discover_plugins(cls) -> dict[str, Plugin]:
        """Discover, validate, and register available plugins.

        Returns:
            Mapping of plugin names and aliases to plugin instances.
        """
        plugins: dict[str, Plugin] = {}

        # Iterate over command entry modules only
        for module_path in cls._iter_plugin_module_paths():
            # Import the plugin module
            plugin_module = cls._import_plugin_module(module_path)
            if plugin_module is None:
                continue

            # Register all exported plugins
            cls._register_module_plugins(plugin_module, plugins)

        return plugins

    @staticmethod
    def _iter_plugin_module_paths() -> list[str]:
        """Collect import paths for dock-discoverable command modules only."""
        module_paths: list[str] = []

        # Iterate over all plugin modules
        for module_info in pkgutil.iter_modules(plugins_dir.__path__,
                                                prefix=f"{plugins_dir.__name__}."):
            if not module_info.ispkg:
                module_paths.append(module_info.name)
                continue

            # Try to import the package
            try:
                package = importlib.import_module(module_info.name)
            except Exception as exc:
                print(
                    f"Failed to import plugin package: {module_info.name} ({exc})")
                continue
            package_path = getattr(package, "__path__", None)
            if package_path is None:
                continue

            # Iterate over all child modules
            for child_info in pkgutil.iter_modules(package_path,
                                                   prefix=f"{module_info.name}."):
                if child_info.ispkg:
                    continue
                module_paths.append(child_info.name)

        module_paths.sort()
        return module_paths

    @staticmethod
    def _import_plugin_module(module_path: str) -> object | None:
        """Import a plugin module safely.

        Args:
            module_path: Fully qualified module import path.

        Returns:
            Imported module object, or `None` when import fails.
        """
        try:
            return importlib.import_module(module_path)
        except Exception as exc:
            print(f"Failed to import plugin: {module_path} ({exc})")
            return None

    @staticmethod
    def _register_module_plugins(
            plugin_module: object,
            plugins: dict[str, Plugin],
    ) -> None:
        """Register all exported plugins from a module.

        Args:
            plugin_module: Imported plugin module.
            plugins: Shared plugin registry.
        """
        # Iterate over all members
        for _, member in inspect.getmembers(plugin_module):
            # Check for export marker
            if not is_exported(member):
                continue

            # Instantiate and validate plugin
            plugin = Dock._build_plugin(member)
            if plugin is None:
                continue

            # Register plugin
            Dock._register_plugin(plugin, plugins)

    @staticmethod
    def _build_plugin(member: object) -> Plugin | None:
        """Instantiate and validate a plugin member.

        Args:
            member: Exported module member.

        Returns:
            Plugin instance when valid, otherwise `None`.
        """
        # Instantiate class members
        if inspect.isclass(member):
            # Is a class
            plugin_class = cast(type[object], member)
            plugin = plugin_class()
        else:
            # Is an instance, maybe a mess, but we'll try
            plugin = cast(Plugin, member)

        # Validate plugin
        spec = getattr(plugin, "spec", None)
        run = getattr(plugin, "run", None)
        if not isinstance(spec, PluginSpec) or not callable(run):
            print(f"Skipped invalid plugin export: {member!r}")
            return None

        return plugin

    @staticmethod
    def _register_plugin(plugin: Plugin,
                         plugins: dict[str, Plugin]) -> None:
        """Register a plugin and all of its aliases.

        Args:
            plugin: Validated plugin instance.
            plugins: Shared plugin registry.
        """
        # Register plugin
        if plugin.spec.name not in plugins:
            plugins[plugin.spec.name] = plugin
        else:
            print(f"Plugin {plugin.spec.name} is already registered")

        # Register aliases
        for alias in plugin.spec.aliases:
            if alias not in plugins:
                plugins[alias] = plugin
            else:
                print(f"Plugin {alias} is already registered")
