#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""Unit tests for sections, platform, and mdk plugins."""

from pathlib import Path
import sys
import tempfile
import unittest

SCRIPTS_DIR = Path(__file__).resolve().parents[1]
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from framework.console import Console
from framework.context import CommandContext, SessionState
from framework.parser import CommandParser
from framework.registry import CommandRegistry
from framework.runner import CommandRunner
from plugins.mdk.mdk import MDKCommand
from plugins.platform.platform import PlatformCommand
from plugins.sections.sections import SectionsCommand


def _run_cmd(command, args: list[str], cwd: Path) -> int:
    """Execute one Command instance in an isolated context."""
    registry = CommandRegistry()
    node = registry.register(command)
    parser = CommandParser("test")
    outcome = parser.parse(node, args)
    if outcome.exit_code is not None:
        return outcome.exit_code
    assert outcome.arguments is not None
    context = CommandContext(
        project_root=cwd,
        session=SessionState(cwd=cwd),
        console=Console(color=False),
        environment={},
    )
    return CommandRunner().run(node, context, outcome.arguments)


class ToolsTests(unittest.TestCase):
    """End-to-end tests for sections, platform, and mdk commands."""

    def test_sections_upsert_and_idempotency(self) -> None:
        toml_text = (
            "[[section]]\n"
            'name = "init"\n'
            'input = ".mr_init.*"\n'
            'output = ".rodata"\n'
            "keep = true\n"
            "sort = true\n"
            'comment = "init table"\n'
        )
        ld_text = (
            "SECTIONS\n"
            "{\n"
            "    .rodata :\n"
            "    {\n"
            "        /* user brace } in comment */\n"
            "        *(.rodata*)\n"
            "    }\n"
            "}\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            cfg = root / "sections.toml"
            ld = root / "link.ld"
            cfg.write_text(toml_text, encoding="utf-8")
            ld.write_text(ld_text, encoding="utf-8")

            self.assertEqual(
                _run_cmd(SectionsCommand(), ["-i", str(ld), "-c", str(cfg)], root),
                0,
            )
            first = ld.read_text(encoding="utf-8")
            self.assertIn("/* @mrX: init table */", first)
            self.assertIn("KEEP(*(SORT(.mr_init.*)))", first)
            self.assertIn("*(.rodata*)", first)

            # Second run should be idempotent
            self.assertEqual(
                _run_cmd(SectionsCommand(), ["-i", str(ld), "-c", str(cfg)], root),
                0,
            )
            self.assertEqual(ld.read_text(encoding="utf-8"), first)

    def test_platform_install_change_detect_and_uninstall(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            src_dir = root / "src"
            dst_dir = root / "dst"
            src_dir.mkdir()
            dst_dir.mkdir()
            (src_dir / "a.c").write_text("new-a", encoding="utf-8")
            (dst_dir / "a.c").write_text("old-a", encoding="utf-8")
            toml_path = root / "board.toml"
            toml_path.write_text(
                '[dir]\nsrc = "src"\ndst = "dst"\n\n'
                '[platform]\nname = "demo"\n\n'
                '[[platform.add]]\nsrc = "a.c"\ndst = "a.c"\n',
                encoding="utf-8",
            )

            self.assertEqual(
                _run_cmd(PlatformCommand(), ["install", "-i", str(toml_path)], root),
                0,
            )
            self.assertEqual((dst_dir / "a.c").read_text(encoding="utf-8"), "new-a")

            # Mutate installed file; uninstall without --force must fail
            (dst_dir / "a.c").write_text("edited", encoding="utf-8")
            self.assertNotEqual(
                _run_cmd(PlatformCommand(), ["uninstall", "-i", str(toml_path)], root),
                0,
            )

            # Uninstall with --force restores original file
            self.assertEqual(
                _run_cmd(PlatformCommand(),
                         ["uninstall", "-i", str(toml_path), "--force"], root),
                0,
            )
            self.assertEqual((dst_dir / "a.c").read_text(encoding="utf-8"), "old-a")

    def test_mdk_add_del_and_rename(self) -> None:
        uvprojx = (
            '<?xml version="1.0" encoding="UTF-8" standalone="no" ?>\n'
            "<Project>\n"
            "  <Targets>\n"
            "    <Target>\n"
            "      <TargetName>Demo</TargetName>\n"
            "      <TargetOption><TargetCommonOption>"
            "<OutputName>Demo</OutputName>"
            "</TargetCommonOption></TargetOption>\n"
            "      <Groups></Groups>\n"
            "    </Target>\n"
            "  </Targets>\n"
            "</Project>\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            proj = root / "Demo.uvprojx"
            src = root / "main.c"
            proj.write_text(uvprojx, encoding="utf-8")
            src.write_text("int main(void) { return 0; }\n", encoding="utf-8")

            self.assertEqual(
                _run_cmd(MDKCommand(),
                         ["add", "-p", str(proj), "-s", str(src)], root),
                0,
            )
            self.assertIn("main.c", proj.read_text(encoding="utf-8"))

            self.assertEqual(
                _run_cmd(MDKCommand(),
                         ["del", "-p", str(proj), "-s", str(src)], root),
                0,
            )
            self.assertNotIn("main.c", proj.read_text(encoding="utf-8"))

            self.assertEqual(
                _run_cmd(MDKCommand(),
                         ["rename", "-p", str(proj), "-n", "Renamed"], root),
                0,
            )
            renamed = root / "Renamed.uvprojx"
            self.assertTrue(renamed.is_file())
            self.assertIn("<TargetName>Renamed</TargetName>",
                          renamed.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
