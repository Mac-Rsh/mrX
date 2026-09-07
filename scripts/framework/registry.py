#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Command tree registration, lookup, aliases, and conflict checking.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from collections.abc import Iterable, Sequence
from dataclasses import dataclass, field
import re

from .plugin import Plugin
from .command import Command

_COMMAND_NAME_PATTERN = re.compile(r"^[a-z][a-z0-9-]*$")
_PLUGIN_NAME_PATTERN = re.compile(r"^[a-z][a-z0-9-]*$")


class CommandRegistrationError(ValueError):
    """Raised when a command cannot be inserted into the registry."""


@dataclass(slots=True)
class CommandNode:
    """One registered command and its direct child lookup tables."""

    command: Command
    path: tuple[str, ...]
    children: dict[str, "CommandNode"] = field(default_factory=dict)
    child_lookup: dict[str, "CommandNode"] = field(default_factory=dict)


class CommandRegistry:
    """Own the validated command tree used by parsing and discovery."""

    def __init__(self) -> None:
        """Create an empty command registry."""
        self._roots: dict[str, CommandNode] = {}
        self._root_lookup: dict[str, CommandNode] = {}
        self._instances: set[int] = set()
        self._plugins: list[Plugin] = []

    def register_plugin(self, plugin: Plugin) -> None:
        """Validate and register every command exported by one plugin."""
        if not _PLUGIN_NAME_PATTERN.fullmatch(plugin.name):
            raise CommandRegistrationError(
                f"Invalid plugin name '{plugin.name}'; "
                "expected lowercase kebab-case"
            )
        if any(registered.name == plugin.name for registered in self._plugins):
            raise CommandRegistrationError(
                f"Plugin '{plugin.name}' is already registered"
            )
        commands = tuple(plugin.commands)
        seen_instances: list[int] = []
        pending_identifiers: list[str] = []

        for command in commands:
            ident = id(command)
            if ident in self._instances or ident in seen_instances:
                raise CommandRegistrationError(
                    f"Plugin '{plugin.name}' registered a duplicate "
                    "command instance"
                )
            seen_instances.append(ident)
            identifiers = (command.spec.name, *command.spec.aliases)
            self._validate_identifiers(command, identifiers)
            for identifier in identifiers:
                if (identifier in self._root_lookup
                        or identifier in pending_identifiers):
                    raise CommandRegistrationError(
                        f"Command identifier '{identifier}' is already "
                        "registered under root"
                    )
                pending_identifiers.append(identifier)

        roots_snapshot = dict(self._roots)
        lookup_snapshot = dict(self._root_lookup)
        instances_snapshot = set(self._instances)
        plugins_snapshot = list(self._plugins)
        try:
            for command in commands:
                self.register(command)
            self._plugins.append(plugin)
        except Exception:
            self._roots = roots_snapshot
            self._root_lookup = lookup_snapshot
            self._instances = instances_snapshot
            self._plugins = plugins_snapshot
            raise

    def register(self, command: Command) -> CommandNode:
        """Register a root command and all declared subcommands."""
        identifiers = (command.spec.name, *command.spec.aliases)
        self._validate_identifiers(command, identifiers)
        for identifier in identifiers:
            if identifier in self._root_lookup:
                raise CommandRegistrationError(
                    f"Command identifier '{identifier}' is already "
                    "registered under root"
                )
        ident = id(command)
        if ident in self._instances:
            raise CommandRegistrationError(
                f"Command '{command.spec.name}' is already registered "
                "as an instance"
            )

        primary: dict[str, CommandNode] = {}
        lookup: dict[str, CommandNode] = {}
        instances_snapshot = set(self._instances)
        try:
            node = self._register_node(command, (), primary, lookup)
        except Exception:
            self._instances = instances_snapshot
            raise
        self._roots.update(primary)
        self._root_lookup.update(lookup)
        self._instances.add(ident)
        return node

    def roots(self, include_hidden: bool = False) -> tuple[CommandNode, ...]:
        """Return root commands sorted by primary name."""
        nodes = self._roots.values()
        if not include_hidden:
            nodes = (node for node in nodes if not node.command.spec.hidden)
        return tuple(sorted(nodes, key=lambda node: node.command.spec.name))

    def resolve(
            self,
            arguments: Sequence[str],
    ) -> tuple[CommandNode, list[str]] | None:
        """Resolve the longest registered command path from raw arguments."""
        if not arguments:
            return None
        node = self._root_lookup.get(arguments[0])
        if node is None:
            return None

        index = 1
        while index < len(arguments):
            child = node.child_lookup.get(arguments[index])
            if child is None:
                break
            node = child
            index += 1
        return node, list(arguments[index:])

    def find(self, path: Sequence[str]) -> CommandNode | None:
        """Find an exact command path using names or aliases."""
        resolved = self.resolve(path)
        if resolved is None:
            return None
        node, remaining = resolved
        return node if not remaining else None

    def lookup_root(self, identifier: str) -> CommandNode | None:
        """Find a root command by primary name or alias."""
        return self._root_lookup.get(identifier)

    def iter_nodes(self, include_hidden: bool = False) -> Iterable[CommandNode]:
        """Iterate over the complete command tree in display order."""
        for root in self.roots(include_hidden=include_hidden):
            yield root
            yield from self._iter_children(root, include_hidden)

    def _register_node(
            self,
            command: Command,
            parent_path: tuple[str, ...],
            primary: dict[str, CommandNode],
            lookup: dict[str, CommandNode],
    ) -> CommandNode:
        """Validate and recursively register one command node."""
        spec = command.spec
        identifiers = (spec.name, *spec.aliases)
        self._validate_identifiers(command, identifiers)
        for identifier in identifiers:
            if identifier in lookup:
                location = " ".join(parent_path) or "root"
                raise CommandRegistrationError(
                    f"Command identifier '{identifier}' is already "
                    f"registered under {location}"
                )

        ident = id(command)
        if parent_path and ident in self._instances:
            raise CommandRegistrationError(
                f"Command '{spec.name}' is already registered as an instance"
            )

        node = CommandNode(command=command, path=(*parent_path, spec.name))
        primary[spec.name] = node
        for identifier in identifiers:
            lookup[identifier] = node
        if parent_path:
            self._instances.add(ident)

        for child in command.subcommands():
            self._register_node(
                child,
                node.path,
                node.children,
                node.child_lookup,
            )
        return node

    @classmethod
    def _iter_children(
            cls,
            node: CommandNode,
            include_hidden: bool,
    ) -> Iterable[CommandNode]:
        """Iterate recursively over sorted child nodes."""
        children = sorted(node.children.values(),
                          key=lambda child: child.command.spec.name)
        for child in children:
            if include_hidden or not child.command.spec.hidden:
                yield child
            yield from cls._iter_children(child, include_hidden)

    @staticmethod
    def _validate_identifier(identifier: str, kind: str) -> None:
        """Validate one command or alias identifier."""
        if identifier.startswith("-"):
            raise CommandRegistrationError(
                f"Invalid {kind} identifier '{identifier}'; "
                "option flags cannot be aliases"
            )
        if not _COMMAND_NAME_PATTERN.fullmatch(identifier):
            raise CommandRegistrationError(
                f"Invalid {kind} identifier '{identifier}'; "
                "expected lowercase kebab-case"
            )

    @classmethod
    def _validate_identifiers(
            cls,
            command: Command,
            identifiers: tuple[str, ...],
    ) -> None:
        """Validate the primary name and unique aliases of one command."""
        cls._validate_identifier(command.spec.name, "command")
        for alias in command.spec.aliases:
            cls._validate_identifier(alias, "alias")
        if len(set(identifiers)) != len(identifiers):
            raise CommandRegistrationError(
                f"Command '{command.spec.name}' declares a duplicate "
                "name or alias"
            )
