#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Interactive command shell backed by prompt_toolkit.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from collections.abc import Callable
from typing import TYPE_CHECKING
import sys

from prompt_toolkit.auto_suggest import AutoSuggestFromHistory
from prompt_toolkit.history import History
from prompt_toolkit.key_binding import KeyBindings
from prompt_toolkit.key_binding.key_processor import KeyPressEvent
from prompt_toolkit import PromptSession

from .completion import RegistryCompleter
from .tokenizer import split_command_line
from .context import SessionState
from .console import Console

if TYPE_CHECKING:
    from .application import Application


class InteractiveShell:
    """Run commands repeatedly with completion, history, and line editing."""

    def __init__(
            self,
            application: "Application",
            session: SessionState,
            console: Console,
            history: History,
            completer: RegistryCompleter,
    ) -> None:
        """Create an interactive shell over one application."""
        self.application = application
        self.session = session
        self.console = console
        self.history = history
        self.completer = completer

    def run(self, execute: Callable[[list[str]], int] | None = None) -> int:
        """Read lines until the user exits or reaches end-of-file."""
        dispatch = execute or (
            lambda arguments: self.application.execute(
                arguments, interactive=True)
        )
        self.session.exit_requested = False
        prompt_session = PromptSession(
            history=self.history,
            completer=self.completer,
            complete_while_typing=False,
            auto_suggest=AutoSuggestFromHistory(),
            enable_history_search=True,
            key_bindings=_key_bindings(),
        )
        self._show_welcome()

        while True:
            try:
                line = prompt_session.prompt("mrX> ")
            except KeyboardInterrupt:
                continue
            except EOFError:
                self.console.write()
                return self.session.last_exit_code

            stripped = line.strip()
            if not stripped:
                continue

            try:
                arguments = split_command_line(stripped)
            except ValueError as exc:
                self.console.warning(str(exc))
                self.session.last_exit_code = 2
                continue

            code = dispatch(arguments)
            self.session.last_exit_code = code
            if self.session.exit_requested:
                return code

    def _show_welcome(self) -> None:
        """Display a short introduction for the interactive shell."""
        self.console.write(
            f"{self.application.name} {self.application.version}")
        self.console.write(
            "Tab completes commands. Up/Down walk history. "
            "Ctrl-R searches history."
        )
        self.console.write(
            "Ctrl-C clears the current line. Ctrl-D"
            + (" or Ctrl-Z" if sys.platform == "win32" else "")
            + " exits."
        )
        self.console.write(
            "Type 'help' or 'list'. Type 'exit' or 'quit' to leave.")


def _key_bindings() -> KeyBindings:
    """Bind Windows Ctrl-Z to end-of-file in addition to Ctrl-D."""
    bindings = KeyBindings()
    if sys.platform != "win32":
        return bindings

    @bindings.add("c-z")
    def _eof(event: KeyPressEvent) -> None:
        """Treat Ctrl-Z as end-of-file on Windows."""
        event.app.exit(exception=EOFError)

    return bindings
