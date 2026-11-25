#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Argparse-driven tab completion for the interactive shell.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from argparse import Action, ArgumentParser, _SubParsersAction
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path
import os

from prompt_toolkit.completion import Completer, Completion, CompleteEvent
from prompt_toolkit.document import Document

from .console import Console
from .context import CommandContext, SessionState, detect_project_root
from .parser import CommandParser, action_is_repeatable, action_takes_value
from .parser import option_actions, positional_actions
from .registry import CommandNode, CommandRegistry
from .tokenizer import CompletionLine, split_for_completion

FILE_COMPLETION = "file"
DIRECTORY_COMPLETION = "directory"
PATH_COMPLETION = "path"


@dataclass(frozen=True, slots=True)
class CompletionItem:
    """One completion candidate shown in the interactive shell."""

    value: str
    description: str = ""
    kind: str = ""


@dataclass(frozen=True, slots=True)
class CompletionRequest:
    """Completion query passed to an optional command-specific completer."""

    tokens: tuple[str, ...]
    prefix: str
    cwd: Path


_GLOBAL_OPTIONS = (
    CompletionItem("--no-color", "Disable colored output", "option"),
    CompletionItem("--verbose", "Show detailed error information", "option"),
    CompletionItem("--help", "Show application help", "option"),
    CompletionItem("-h", "Show application help", "option"),
    CompletionItem("--version", "Show the mrX version", "option"),
)
_CONTINUING_GLOBAL_OPTIONS = frozenset({"--no-color", "--verbose"})
_TERMINAL_GLOBAL_OPTIONS = frozenset({"--help", "-h", "--version"})


def attach_path_completion(action: Action,
                           kind: str = FILE_COMPLETION) -> Action:
    """Mark an argparse action as a file, directory, or generic path."""
    action.completion_kind = kind  # type: ignore[attr-defined]
    return action


def path_completion_kind(action: Action) -> str | None:
    """Return the path completion kind attached to an action, if any."""
    kind = getattr(action, "completion_kind", None)
    return kind if isinstance(kind, str) else None


class RegistryCompleter(Completer):
    """Complete command names, options, choices, and paths from the registry."""

    def __init__(
            self,
            registry: CommandRegistry,
            parser_factory: CommandParser,
            session: SessionState,
    ) -> None:
        """Create a completer bound to one registry and session."""
        self.registry = registry
        self.parser_factory = parser_factory
        self.session = session

    def get_completions(
            self,
            document: Document,
            complete_event: CompleteEvent,
    ) -> Iterable[Completion]:
        """Yield prompt_toolkit completions for the text before the cursor."""
        del complete_event
        line = document.text_before_cursor
        split = split_for_completion(line)
        items = self.complete_line(split)
        insert_from = split.current_start - len(line)
        for item in items:
            yield Completion(
                text=_insert_text(item.value, split.quote),
                start_position=insert_from,
                display=item.value,
                display_meta=item.description,
            )

    def complete_line(self, split: CompletionLine) -> list[CompletionItem]:
        """Return completion items for one tokenized prefix."""
        finished, used_globals, terminal = _consume_global_options(
            split.finished)
        if terminal:
            return []
        if not finished:
            if split.prefix.startswith("-"):
                return _complete_global_options(split.prefix, used_globals)
            return self._complete_commands(split.prefix)

        resolved = self.registry.resolve(finished)
        if resolved is None:
            return []

        node, remaining = resolved
        if not remaining and not split.prefix.startswith("-"):
            child_items = self._complete_children(node, split.prefix)
        else:
            child_items = []

        parser = self.parser_factory.build(node)
        request = CompletionRequest(
            tokens=tuple(remaining),
            prefix=split.prefix,
            cwd=self.session.cwd,
        )
        items = list(child_items)
        items.extend(node.command.complete(
            _completion_context(self.session), request))
        items.extend(complete_parser(parser, remaining, split.prefix,
                                     self.session.cwd))
        return _unique_items(_filter_items(items, split.prefix))

    def _complete_commands(self, prefix: str) -> list[CompletionItem]:
        """Complete root command names and aliases."""
        items: list[CompletionItem] = []
        for node in self.registry.roots():
            spec = node.command.spec
            if _matches(spec.name, prefix):
                items.append(CompletionItem(
                    value=spec.name,
                    description=spec.description,
                    kind="command",
                ))
            for alias in spec.aliases:
                if _matches(alias, prefix):
                    items.append(CompletionItem(
                        value=alias,
                        description=f"alias of {spec.name}",
                        kind="alias",
                    ))
        return items

    @staticmethod
    def _complete_children(node: CommandNode,
                           prefix: str) -> list[CompletionItem]:
        """Complete registered child command names and aliases."""
        items: list[CompletionItem] = []
        for child in node.children.values():
            spec = child.command.spec
            if spec.hidden:
                continue
            if _matches(spec.name, prefix):
                items.append(CompletionItem(
                    value=spec.name,
                    description=spec.description,
                    kind="command",
                ))
            for alias in spec.aliases:
                if _matches(alias, prefix):
                    items.append(CompletionItem(
                        value=alias,
                        description=f"alias of {spec.name}",
                        kind="alias",
                    ))
        return items


def complete_parser(
        parser: ArgumentParser,
        consumed: list[str],
        prefix: str,
        cwd: Path,
) -> list[CompletionItem]:
    """Complete options, choices, and paths for one argparse parser."""
    options = {flag: action for action in option_actions(parser)
               for flag in action.option_strings}
    positionals = positional_actions(parser)
    used: set[int] = set()
    positional_index = 0
    expecting: Action | None = None
    end_of_options = False

    index = 0
    while index < len(consumed):
        token = consumed[index]
        if expecting is not None:
            expecting = None
            index += 1
            continue
        if not end_of_options and token == "--":
            end_of_options = True
            index += 1
            continue
        option = None if end_of_options else _match_option(token, options)
        if option is not None:
            action = options[option]
            if not action_is_repeatable(action):
                used.add(id(action))
            if action_takes_value(action) and "=" not in token:
                expecting = action
            index += 1
            continue
        positional_index += 1
        index += 1

    if expecting is not None:
        return _complete_action_value(expecting, prefix, cwd)

    if not end_of_options:
        inline_option = _split_inline_option(prefix, options)
        if inline_option is not None:
            flag, action, value_prefix = inline_option
            return [
                CompletionItem(
                    value=f"{flag}={item.value}",
                    description=item.description,
                    kind=item.kind,
                )
                for item in _complete_action_value(action, value_prefix, cwd)
            ]

    items: list[CompletionItem] = []
    if not end_of_options and prefix.startswith("-"):
        items.extend(_complete_options(parser, used, prefix))
        return items

    positional = _next_positional(positionals, positional_index)
    if positional is not None:
        items.extend(_complete_action_value(positional, prefix, cwd))
    return items


def _complete_options(parser: ArgumentParser, used: set[int],
                      prefix: str) -> list[CompletionItem]:
    """Complete unused option strings."""
    items: list[CompletionItem] = []
    for action in option_actions(parser):
        if id(action) in used and not action_is_repeatable(action):
            continue
        help_text = (action.help or "").strip()
        for flag in action.option_strings:
            if _matches(flag, prefix):
                items.append(CompletionItem(
                    value=flag,
                    description=help_text,
                    kind="option",
                ))
    return items


def _complete_action_value(action: Action, prefix: str,
                           cwd: Path) -> list[CompletionItem]:
    """Complete choices or paths for one argparse action."""
    if isinstance(action, _SubParsersAction):
        return [
            CompletionItem(value=str(name),
                           description=_subcommand_help(action, name),
                           kind="value")
            for name in action.choices
            if _matches(str(name), prefix)
        ]
    if action.choices:
        return [
            CompletionItem(value=str(choice), description="", kind="value")
            for choice in action.choices
            if _matches(str(choice), prefix)
        ]
    kind = path_completion_kind(action)
    if kind:
        return complete_paths(prefix, cwd, kind)
    return []


def complete_paths(prefix: str, cwd: Path, kind: str) -> list[CompletionItem]:
    """Complete files and directories relative to the session cwd."""
    separator = _preferred_separator(prefix)
    directory, name_prefix, display_dir = _split_path_prefix(prefix, cwd)
    if directory is None or not directory.is_dir():
        return []

    items: list[CompletionItem] = []
    try:
        entries = list(directory.iterdir())
    except OSError:
        return []

    for entry in sorted(entries, key=lambda path: path.name.lower()):
        if not _matches(entry.name, name_prefix):
            continue
        is_dir = entry.is_dir()
        if kind == FILE_COMPLETION and is_dir:
            value = _join_display(display_dir, entry.name,
                                  separator) + separator
            items.append(CompletionItem(
                value=value, description="directory", kind="path"))
            continue
        if kind == DIRECTORY_COMPLETION and not is_dir:
            continue
        suffix = separator if is_dir else ""
        value = _join_display(display_dir, entry.name, separator) + suffix
        description = "directory" if is_dir else "file"
        items.append(CompletionItem(
            value=value, description=description, kind="path"))
    return items


def _split_path_prefix(prefix: str, cwd: Path) -> tuple[Path | None, str, str]:
    """Split a typed path into a directory to list and a name prefix."""
    if not prefix:
        return cwd, "", ""

    if prefix in {".", ".."}:
        directory = (cwd / prefix).resolve()
        sep = _preferred_separator(prefix)
        return directory, "", f"{prefix}{sep}"

    sep_index = max(prefix.rfind("/"), prefix.rfind("\\"))
    if sep_index >= 0:
        dir_part = prefix[:sep_index + 1]
        name = prefix[sep_index + 1:]
        normalized_dir = dir_part.replace("/", os.sep).replace("\\", os.sep)
        path = Path(normalized_dir)
        directory = path if path.is_absolute() else (cwd / path)
        try:
            return directory.resolve(), name, dir_part
        except OSError:
            return None, name, dir_part

    if len(prefix) >= 2 and prefix[1] == ":" and prefix[0].isalpha():
        drive_part = prefix[:2]
        name = prefix[2:]
        try:
            return Path(drive_part + os.sep).resolve(), name, drive_part + os.sep
        except OSError:
            return None, name, drive_part

    return cwd, prefix, ""


def _preferred_separator(prefix: str) -> str:
    """Reuse the separator style the user is already typing."""
    if "\\" in prefix and "/" not in prefix:
        return "\\"
    if "/" in prefix and "\\" not in prefix:
        return "/"
    return os.sep


def _join_display(directory: str, name: str, separator: str) -> str:
    """Join a typed directory prefix with a completion name."""
    if not directory:
        return name
    if directory.endswith(("/", "\\")):
        return f"{directory}{name}"
    return f"{directory}{separator}{name}"


def _match_option(token: str, options: dict[str, Action]) -> str | None:
    """Return the option string used by a consumed token."""
    if not token.startswith("-"):
        return None
    if token in options:
        return token
    if "=" in token:
        flag, _, _ = token.partition("=")
        if flag in options:
            return flag
    return None


def _split_inline_option(
        prefix: str,
        options: dict[str, Action],
) -> tuple[str, Action, str] | None:
    """Split a current `--option=value` prefix for value completion."""
    if "=" not in prefix:
        return None
    flag, _, value_prefix = prefix.partition("=")
    action = options.get(flag)
    if action is None or not action_takes_value(action):
        return None
    return flag, action, value_prefix


def _consume_global_options(
        tokens: tuple[str, ...],
) -> tuple[tuple[str, ...], set[str], bool]:
    """Consume leading global options before resolving a command path."""
    used: set[str] = set()
    index = 0
    while index < len(tokens):
        token = tokens[index]
        if token in _CONTINUING_GLOBAL_OPTIONS:
            used.add(token)
            index += 1
            continue
        if token in _TERMINAL_GLOBAL_OPTIONS:
            used.add(token)
            return tokens[index + 1:], used, True
        break
    return tokens[index:], used, False


def _complete_global_options(
        prefix: str,
        used: set[str],
) -> list[CompletionItem]:
    """Complete unused global options before the root command."""
    return [
        item for item in _GLOBAL_OPTIONS
        if item.value not in used and _matches(item.value, prefix)
    ]


def _next_positional(positionals: list[Action], index: int) -> Action | None:
    """Return the positional action that should be completed next."""
    if index < len(positionals):
        return positionals[index]
    if positionals:
        last = positionals[-1]
        if last.nargs in {"*", "+"}:
            return last
    return None


def _subcommand_help(action: _SubParsersAction, name: object) -> str:
    """Return help text for one argparse subparser choice."""
    choice = action.choices.get(name)
    if choice is None:
        return ""
    return (getattr(choice, "description", None) or "").strip()


def _matches(value: str, prefix: str) -> bool:
    """Match completion values case-insensitively."""
    if not prefix:
        return True
    return value.lower().startswith(prefix.lower())


def _filter_items(items: Iterable[CompletionItem],
                  prefix: str) -> list[CompletionItem]:
    """Filter custom completion items by the current prefix."""
    return [item for item in items if _matches(item.value, prefix)]


def _unique_items(items: Iterable[CompletionItem]) -> list[CompletionItem]:
    """Drop duplicate completion values while preserving order."""
    seen: set[str] = set()
    unique: list[CompletionItem] = []
    for item in items:
        key = item.value.lower()
        if key in seen:
            continue
        seen.add(key)
        unique.append(item)
    return unique


def _insert_text(value: str, quote: str | None) -> str:
    """Build the inserted completion text, quoting when needed."""
    if quote:
        escaped = value.replace(quote, quote * 2)
        return f"{quote}{escaped}{quote}"
    if _needs_quotes(value):
        escaped = value.replace('"', '""')
        return f'"{escaped}"'
    return value


def _needs_quotes(value: str) -> bool:
    """Return whether a completion value contains token delimiters."""
    return any(char in value for char in " \t\"'")


def _completion_context(session: SessionState) -> CommandContext:
    """Build a lightweight context used only by custom completers."""
    return CommandContext(
        project_root=detect_project_root(),
        session=session,
        environment={},
        console=Console(),
        interactive=True,
    )
