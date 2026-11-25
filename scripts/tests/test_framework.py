#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""Unit tests for the mrX CLI framework."""

from argparse import ArgumentParser, Namespace
from pathlib import Path
import sys
import tempfile
import unittest

SCRIPTS_DIR = Path(__file__).resolve().parents[1]
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from framework.command import Command, CommandResult, CommandSpec
from framework.completion import _split_path_prefix, complete_parser
from framework.console import Console
from framework.context import CommandContext, SessionState
from framework.diagnostics import Diagnostic, DiagnosticLevel
from framework.parser import CommandParser, ParserExit, adopt_parser
from framework.parser import action_is_repeatable, positional_actions
from framework.registry import CommandRegistrationError, CommandRegistry
from framework.runner import CommandRunner
from framework.tokenizer import split_for_completion


class _DummyCommand(Command):
    """Minimal command for registry and runner tests."""

    def __init__(
            self,
            name: str,
            *,
            hidden: bool = False,
            children: tuple[Command, ...] = (),
            result: CommandResult | None = None,
    ) -> None:
        self.spec = CommandSpec(name=name, description=name, hidden=hidden)
        self._children = children
        self._result = result or CommandResult.success()

    def subcommands(self) -> tuple[Command, ...]:
        return self._children

    def execute(self, context: CommandContext,
                arguments: Namespace) -> CommandResult:
        del context, arguments
        return self._result


class _SelfReferentialCommand(Command):
    """Command whose subcommands() yields itself."""

    spec = CommandSpec(name="loop", description="loop")

    def subcommands(self) -> tuple[Command, ...]:
        return (self,)

    def execute(self, context: CommandContext,
                arguments: Namespace) -> CommandResult:
        del context, arguments
        return CommandResult.success()


class FrameworkTests(unittest.TestCase):
    """Regression tests for framework components."""

    def test_hidden_parent_hides_visible_children(self) -> None:
        registry = CommandRegistry()
        visible_child = _DummyCommand("child", hidden=False)
        hidden_parent = _DummyCommand("secret", hidden=True,
                                      children=(visible_child,))
        visible_root = _DummyCommand("pub", hidden=False)
        registry.register(hidden_parent)
        registry.register(visible_root)

        names = [node.path for node in registry.iter_nodes(include_hidden=False)]
        self.assertEqual(names, [("pub",)])

        all_names = [node.path for node in registry.iter_nodes(include_hidden=True)]
        self.assertEqual(all_names, [("pub",), ("secret",), ("secret", "child")])

    def test_self_referential_root_command_rejected(self) -> None:
        registry = CommandRegistry()
        with self.assertRaises(CommandRegistrationError):
            registry.register(_SelfReferentialCommand())

    def test_adopt_parser_protects_subparsers(self) -> None:
        parser = ArgumentParser(prog="test")
        subparsers = parser.add_subparsers(dest="sub")
        sub = subparsers.add_parser("run")
        sub.add_argument("--flag", required=True)
        adopt_parser(parser)

        with self.assertRaises(ParserExit) as ctx:
            parser.parse_args(["run"])
        self.assertEqual(ctx.exception.status, 2)

    def test_positional_named_help_and_extend_action(self) -> None:
        parser = ArgumentParser(prog="test")
        parser.add_argument("help")
        extend_action = parser.add_argument("--item", action="extend", nargs="+")

        pos_dests = [action.dest for action in positional_actions(parser)]
        self.assertEqual(pos_dests, ["help"])
        self.assertTrue(action_is_repeatable(extend_action))

    def test_complete_parser_respects_end_of_options_marker(self) -> None:
        parser = ArgumentParser(prog="test")
        parser.add_argument("--flag", action="store_true")
        parser.add_argument("target", choices=("--literal", "normal"))

        items = complete_parser(parser, ["--"], "--", Path.cwd())
        values = [item.value for item in items]
        self.assertEqual(values, ["--literal"])

    def test_split_path_prefix_edge_cases(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            cwd = Path(tmp)
            (cwd / "foo").mkdir()

            directory, name, display = _split_path_prefix("foo/.", cwd)
            self.assertEqual(directory, (cwd / "foo").resolve())
            self.assertEqual(name, ".")
            self.assertEqual(display, "foo/")

            directory_dot, name_dot, display_dot = _split_path_prefix(".", cwd)
            self.assertEqual(directory_dot, cwd.resolve())
            self.assertEqual(name_dot, "")
            self.assertTrue(display_dot.startswith("."))

            directory_parent, name_parent, display_parent = _split_path_prefix("..", cwd)
            self.assertEqual(directory_parent, cwd.parent.resolve())
            self.assertEqual(name_parent, "")
            self.assertTrue(display_parent.startswith(".."))

    def test_completion_line_quote(self) -> None:
        line = split_for_completion('cmd "hello wor')
        self.assertEqual(line.prefix, "hello wor")
        self.assertEqual(line.quote, '"')

    def test_runner_renders_failure_message_with_diagnostics(self) -> None:
        class _CaptureConsole(Console):
            def __init__(self) -> None:
                super().__init__(color=False)
                self.errors: list[str] = []
                self.diags: list[Diagnostic] = []

            def error(self, message: str) -> None:
                self.errors.append(message)

            def diagnostic(self, diagnostic: Diagnostic) -> None:
                self.diags.append(diagnostic)

        console = _CaptureConsole()
        diag = Diagnostic(level=DiagnosticLevel.ERROR, code="E1", message="detail")
        cmd = _DummyCommand(
            "fail",
            result=CommandResult(
                exit_code=1,
                message="top-level failure",
                diagnostics=(diag,),
            ),
        )
        registry = CommandRegistry()
        node = registry.register(cmd)
        parser = CommandParser("test")
        outcome = parser.parse(node, [])
        assert outcome.arguments is not None
        context = CommandContext(
            project_root=Path.cwd(),
            session=SessionState(cwd=Path.cwd()),
            console=console,
            environment={},
        )
        exit_code = CommandRunner().run(node, context, outcome.arguments)
        self.assertEqual(exit_code, 1)
        self.assertEqual(len(console.diags), 1)
        self.assertEqual(console.errors, ["top-level failure"])

    def test_subcommands_appear_in_help_and_help_completion(self) -> None:
        from framework.builtins import HelpCommand
        from framework.completion import CompletionRequest

        registry = CommandRegistry()
        child = _DummyCommand("sub", hidden=False)
        parent = _DummyCommand("parent", children=(child,))
        node = registry.register(parent)
        parser = CommandParser("test")

        help_text = parser.format_help(node)
        self.assertIn("subcommands:", help_text)
        self.assertIn("sub", help_text)

        help_cmd = HelpCommand(registry, parser, "test")
        context = CommandContext(
            project_root=Path.cwd(),
            session=SessionState(cwd=Path.cwd()),
            console=Console(color=False),
            environment={},
        )
        items = help_cmd.complete(
            context,
            CompletionRequest(tokens=("parent",), prefix="s", cwd=Path.cwd()),
        )
        self.assertEqual([item.value for item in items], ["sub"])


if __name__ == "__main__":
    unittest.main()
