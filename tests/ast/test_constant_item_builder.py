#!/usr/bin/env python3
"""Check constant declaration ASTs; resolution and type checks come later.

Run after building: python3 tests/ast/test_constant_item_builder.py
Run all AST suites: python3 -m unittest discover -s tests/ast -p 'test_*builder.py'
"""

from pathlib import Path
import subprocess
import tempfile
import unittest

from test_expression_builder import COMPILER, dump


def constant(name, type_name, value):
    return ("ConstItem: " + name, ("Type:", "TypeRef: " + type_name), ("Value:", value))


class ConstantItemBuilderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="rx-const-ast-")
        cls.addClassCleanup(cls.directory.cleanup)
        cls.source = Path(cls.directory.name) / "case.rx"

    def compile(self, source):
        self.source.write_text(source)
        return subprocess.run(
            [str(COMPILER), "--stage", "semantic", str(self.source)],
            capture_output=True, text=True, timeout=10,
        )

    def check_ast(self, source, *items):
        result = self.compile(source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, dump(("Crate", *items)))

    def test_integer_constants_preserve_types_and_literal_spelling(self):
        for name, type_name, literal in [
            ("N", "usize", "3"),
            ("SIGNED", "i32", "42i32"),
            ("HEX", "u32", "0xDeAd_BeEfu32"),
            ("BINARY", "isize", "0b_1010_isize"),
            ("OCTAL", "usize", "0o7_7usize"),
            ("LARGE", "u32", "18446744073709551616u32"),
        ]:
            with self.subTest(name=name, literal=literal):
                self.check_ast("const " + name + ": " + type_name + " = " + literal + ";",
                    constant(name, type_name, "IntegerLiteral: " + literal))

    def test_boolean_constants_and_parentheses(self):
        for value, expected in [("true", "Boolean: true"), ("false", "Boolean: false"),
                                ("(((true)))", "Boolean: true"), ("((false))", "Boolean: false")]:
            with self.subTest(value=value):
                self.check_ast("const FLAG: bool = " + value + ";", constant("FLAG", "bool", expected))

    def test_negative_constants_preserve_unary_expression(self):
        for value, magnitude in [("-3", "3"), ("-((3))", "3"), ("(-((3)))", "3"),
                                 ("-0x8000_0000i32", "0x8000_0000i32"),
                                 ("-(0b_1000_isize)", "0b_1000_isize")]:
            with self.subTest(value=value):
                self.check_ast("const NEG: i32 = " + value + ";",
                    constant("NEG", "i32", ("UnaryExpr: -", "IntegerLiteral: " + magnitude)))

    def test_constant_paths_remain_unresolved(self):
        # Even a forward reference remains a PathExpr; the builder does not
        # perform lookup or reject names missing from a symbol table.
        self.check_ast("const M: usize = (((N))); const N: usize = 3;",
            constant("M", "usize", "PathExpr: N"),
            constant("N", "usize", "IntegerLiteral: 3"))
        self.check_ast("const NEG: i32 = -((MISSING));",
            constant("NEG", "i32", ("UnaryExpr: -", "PathExpr: MISSING")))

    def test_constant_function_order_and_array_count(self):
        self.check_ast("const N: usize = 3; fn main() { let a = [0; N]; a[0] } const FLAG: bool = true;",
            constant("N", "usize", "IntegerLiteral: 3"),
            ("Function: main", ("Block", ("LetStmt: a", ("ArrayRepeatExpr",
                ("Value:", "IntegerLiteral: 0"), ("Count:", "PathExpr: N"))),
                ("IndexExpr", ("Base:", "PathExpr: a"), ("Index:", "IntegerLiteral: 0")))),
            constant("FLAG", "bool", "Boolean: true"))

    def test_existing_type_paths_and_unit_type(self):
        self.check_ast("const VALUE: types::Type = OTHER; const UNIT: () = EMPTY;",
            constant("VALUE", "types::Type", "PathExpr: OTHER"),
            constant("UNIT", "()", "PathExpr: EMPTY"))

    def test_malformed_declarations_rejected(self):
        for source in ["const : i32 = 1;", "const N i32 = 1;", "const N: = 1;",
                       "const N: i32 1;", "const N: i32 = ;", "const N: i32 = 1",
                       "const N: i32 = 1 + 2;", "const N: i32 = -true;",
                       "const N: i32 = --3;", "const N: i32 = 0xG;",
                       "const N: i32 = 3u64;"]:
            with self.subTest(source=source):
                result = self.compile(source)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertIn(": error:", result.stderr)

    def test_unsupported_type_and_item_forms_remain_diagnostic(self):
        for source, diagnostic in [
            ("const N: &i32 = OTHER;", "this type form is not supported yet"),
            ("struct S {}", "this item form is not supported yet"),
        ]:
            with self.subTest(source=source):
                result = self.compile(source)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(result.stderr, "error: " + diagnostic + "\n")


if __name__ == "__main__":
    unittest.main()
