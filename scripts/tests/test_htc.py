#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""Unit tests for the HTC compiler plugin."""

from pathlib import Path
import sys
import tempfile
import unittest

SCRIPTS_DIR = Path(__file__).resolve().parents[1]
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from plugins.htc.common.diagnostics import Diagnostics
from plugins.htc.common.syntax import ArrayMode, ArrayValue, ByteStringValue
from plugins.htc.compiler import CompileOptions, HTCCompiler
from plugins.htc.frontend.directives import DefineDirective, DirectiveParser
from plugins.htc.frontend.lexer import Lexer
from plugins.htc.frontend.parser import Parser
from plugins.htc.semantic.builder import SemanticBuilder


class HTCTests(unittest.TestCase):
    """Regression tests for HTC preprocessor, parser, semantic, and codegen."""

    def test_directive_parser_tabs_and_escaped_quotes(self) -> None:
        parser = DirectiveParser()
        directive = parser.parse('#define\tFOO\t"a\\"b" // trailing comment')
        self.assertIsInstance(directive, DefineDirective)
        assert isinstance(directive, DefineDirective)
        self.assertEqual(directive.name, "FOO")
        self.assertEqual(directive.value, '"a\\"b"')

    def test_inactive_if_branch_not_evaluated_and_octal_condition(self) -> None:
        source = (
            "#if 0\n"
            "#if 1 / 0\n"
            "#endif\n"
            "#endif\n"
            "#if 010 == 8\n"
            "/ {\n"
            "    ok = 1;\n"
            "};\n"
            "#endif\n"
        )
        compiler = HTCCompiler()
        with tempfile.TemporaryDirectory() as tmp:
            input_path = Path(tmp) / "test.hts"
            output_path = Path(tmp) / "test.h"
            input_path.write_text(source, encoding="utf-8")
            result = compiler.compile(CompileOptions(
                input_path=input_path, output_path=output_path, use_cpp=False))
            self.assertFalse(result.diagnostics.has_errors(),
                             result.diagnostics.format_all())
            assert result.header_text is not None
            self.assertIn("__MR_HT_N_P_ok", result.header_text)

    def test_unterminated_nested_conditionals_report_all_frames(self) -> None:
        source = "#if 1\n#if 1\n"
        compiler = HTCCompiler()
        with tempfile.TemporaryDirectory() as tmp:
            input_path = Path(tmp) / "unterminated.hts"
            output_path = Path(tmp) / "unterminated.h"
            input_path.write_text(source, encoding="utf-8")
            result = compiler.compile(CompileOptions(
                input_path=input_path, output_path=output_path, use_cpp=False))
            self.assertEqual(len(result.diagnostics.diagnostics), 2)

    def test_inline_array_vs_dts_bytestring_disambiguation(self) -> None:
        source = (
            "/ {\n"
            "    legacy = [10, 20];\n"
            "    bytes = [00 11 ff];\n"
            "};\n"
        )
        diagnostics = Diagnostics()
        tokens = Lexer(source, "<test>", diagnostics).tokenize()
        doc = Parser(tokens, diagnostics).parse()
        self.assertFalse(diagnostics.has_errors(), diagnostics.format_all())
        props = {prop.name: prop.value for prop in doc.roots[0].properties}
        self.assertIsInstance(props["legacy"], ArrayValue)
        assert isinstance(props["legacy"], ArrayValue)
        self.assertEqual(props["legacy"].mode, ArrayMode.INLINE)
        self.assertEqual(len(props["legacy"].elements), 2)
        self.assertIsInstance(props["bytes"], ByteStringValue)

    def test_delete_property_before_readd_in_root(self) -> None:
        source = (
            "/ {\n"
            "    x = 1;\n"
            "};\n"
            "/ {\n"
            "    /delete-property/ x;\n"
            "    x = 2;\n"
            "};\n"
        )
        diagnostics = Diagnostics()
        tokens = Lexer(source, "<test>", diagnostics).tokenize()
        doc = Parser(tokens, diagnostics).parse()
        tree = SemanticBuilder(diagnostics).build(doc)
        self.assertFalse(diagnostics.has_errors(), diagnostics.format_all())
        root = tree.nodes_by_path["/"]
        props = [p for p in root.properties if p.name == "x"]
        self.assertEqual(len(props), 1)

    def test_delete_property_before_readd_in_child_node(self) -> None:
        source = (
            "/ {\n"
            "    child {\n"
            "        /delete-property/ x;\n"
            "        x = 1;\n"
            "    };\n"
            "};\n"
        )
        diagnostics = Diagnostics()
        tokens = Lexer(source, "<test>", diagnostics).tokenize()
        doc = Parser(tokens, diagnostics).parse()
        tree = SemanticBuilder(diagnostics).build(doc)
        self.assertFalse(diagnostics.has_errors(), diagnostics.format_all())
        child = tree.nodes_by_path["/child"]
        self.assertEqual([p.name for p in child.properties], ["x"])

    def test_directive_inside_multiline_block_comment_ignored(self) -> None:
        source = (
            "/*\n"
            "#define IGNORED 1\n"
            "#if 1 / 0\n"
            "*/\n"
            "/ {\n"
            "    val = 1;\n"
            "};\n"
        )
        compiler = HTCCompiler()
        with tempfile.TemporaryDirectory() as tmp:
            input_path = Path(tmp) / "comment.hts"
            output_path = Path(tmp) / "comment.h"
            input_path.write_text(source, encoding="utf-8")
            result = compiler.compile(CompileOptions(
                input_path=input_path, output_path=output_path, use_cpp=False))
            self.assertFalse(result.diagnostics.has_errors(),
                             result.diagnostics.format_all())

    def test_prio_leading_zero_and_c_string_escapes(self) -> None:
        source = (
            "/ {\n"
            "    dev0 {\n"
            '        status = "prio(08)";\n'
            '        msg = "a\\0b\\x1b";\n'
            "    };\n"
            "};\n"
        )
        compiler = HTCCompiler()
        with tempfile.TemporaryDirectory() as tmp:
            input_path = Path(tmp) / "prio.hts"
            output_path = Path(tmp) / "prio.h"
            input_path.write_text(source, encoding="utf-8")
            result = compiler.compile(CompileOptions(
                input_path=input_path, output_path=output_path, use_cpp=False))
            self.assertFalse(result.diagnostics.has_errors(),
                             result.diagnostics.format_all())
            assert result.header_text is not None
            self.assertIn("MR_DRIVER_LEVEL_PRIO(8)", result.header_text)
            self.assertNotIn("MR_DRIVER_LEVEL_PRIO(08)", result.header_text)
            self.assertIn('"a\\0b\\x1b"', result.header_text)

    def test_override_duplicate_property_reported(self) -> None:
        source = (
            "/ {\n"
            "    uart0: uart0 {\n"
            "        baud = 115200;\n"
            "    };\n"
            "};\n"
            "&uart0 {\n"
            "    baud = 9600;\n"
            "    baud = 19200;\n"
            "};\n"
        )
        diagnostics = Diagnostics()
        tokens = Lexer(source, "<test>", diagnostics).tokenize()
        doc = Parser(tokens, diagnostics).parse()
        SemanticBuilder(diagnostics).build(doc)
        self.assertTrue(diagnostics.has_errors())

    def test_fixture_driver_hts_compiles(self) -> None:
        fixture = Path(__file__).resolve().parents[2] / "test" / "drivers" / "fixtures" / "driver.hts"
        if not fixture.is_file():
            self.skipTest("driver.hts fixture not found")
        with tempfile.TemporaryDirectory() as tmp:
            result = HTCCompiler().compile(CompileOptions(
                input_path=fixture,
                output_path=Path(tmp) / "driver.h",
                use_cpp=False,
            ))
            self.assertFalse(result.diagnostics.has_errors(),
                             result.diagnostics.format_all())


if __name__ == "__main__":
    unittest.main()
