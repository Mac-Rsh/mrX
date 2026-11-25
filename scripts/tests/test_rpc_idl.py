#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""Unit tests for the RPC IDL compiler plugin."""

from pathlib import Path
import sys
import tempfile
import unittest

SCRIPTS_DIR = Path(__file__).resolve().parents[1]
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from plugins.htc.common.diagnostics import Diagnostics
from plugins.rpc_idl.backend.symbols import CSymbolChecker
from plugins.rpc_idl.compiler import CompileOptions, RpcIdlCompiler
from plugins.rpc_idl.frontend.lexer import Lexer
from plugins.rpc_idl.frontend.parser import Parser
from plugins.rpc_idl.frontend.syntax import StructDecl
from plugins.rpc_idl.semantic.builder import SemanticBuilder


class RpcIdlTests(unittest.TestCase):
    """Regression tests for RPC IDL parser, semantic, and symbol checking."""

    def test_unclosed_struct_does_not_swallow_next_struct(self) -> None:
        source = (
            "struct Broken {\n"
            "    x: u32;\n"
            "struct Valid {\n"
            "    y: u32;\n"
            "};\n"
        )
        diagnostics = Diagnostics()
        tokens = Lexer(source, "<test>", diagnostics).tokenize()
        doc = Parser(tokens, diagnostics).parse()
        self.assertTrue(diagnostics.has_errors())
        struct_names = [
            decl.name for decl in doc.declarations
            if isinstance(decl, StructDecl)
        ]
        self.assertEqual(struct_names, ["Valid"])

    def test_out_of_order_service_ids_do_not_collide(self) -> None:
        source = (
            "service A @1 { ping(); };\n"
            "service B @0 { pong(); };\n"
            "service C { echo(); };\n"
        )
        diagnostics = Diagnostics()
        tokens = Lexer(source, "<test>", diagnostics).tokenize()
        doc = Parser(tokens, diagnostics).parse()
        schema = SemanticBuilder(diagnostics).build(doc)
        self.assertFalse(diagnostics.has_errors(), diagnostics.format_all())
        self.assertEqual(schema.services["C"].service_id, 2)

    def test_self_desc_symbol_collision_detected(self) -> None:
        source = "service rpc { decode_self_desc(); };\n"
        diagnostics = Diagnostics()
        tokens = Lexer(source, "<test>", diagnostics).tokenize()
        doc = Parser(tokens, diagnostics).parse()
        schema = SemanticBuilder(diagnostics).build(doc)
        CSymbolChecker(diagnostics).check(schema)
        self.assertTrue(diagnostics.has_errors())

    def test_fixture_rpc_idl_compiles(self) -> None:
        fixture = Path(__file__).resolve().parents[2] / "test" / "modules" / "rpc" / "rpc.idl"
        if not fixture.is_file():
            self.skipTest("rpc.idl fixture not found")
        result = RpcIdlCompiler().compile(CompileOptions(
            input_path=fixture,
            use_cpp=False,
        ))
        self.assertFalse(result.diagnostics.has_errors(),
                         result.diagnostics.format_all())

    def test_coalesced_fixed_slot_uses_byte_pointer_offset(self) -> None:
        source = (
            "service demo {\n"
            "    op(items: u8<4>, a: u16, b: u16) -> (ok: u8);\n"
            "};\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            idl_path = Path(tmp) / "demo.idl"
            idl_path.write_text(source, encoding="utf-8")
            result = RpcIdlCompiler().compile(CompileOptions(
                input_path=idl_path,
                use_cpp=False,
            ))
            self.assertFalse(result.diagnostics.has_errors(),
                             result.diagnostics.format_all())
            assert result.source_text is not None
            self.assertIn("mr_memmove(p + ", result.source_text)
            self.assertNotIn("mr_memmove(&request->a", result.source_text)


if __name__ == "__main__":
    unittest.main()
