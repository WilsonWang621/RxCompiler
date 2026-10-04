#!/usr/bin/env python3
"""Check AST structure only; constant values and types belong to semantic analysis.

Run after building: python3 tests/ast/test_expression_builder.py
Set RX_AST_COMPILER to check another compiler executable.
"""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get("RX_AST_COMPILER", ROOT / "target/compiler"))


def dump(node, indent=0):
    if isinstance(node, str):
        return " " * indent + node + "\n"
    label, *children = node
    return " " * indent + label + "\n" + "".join(
        dump(child, indent + 1) for child in children
    )


class ExpressionBuilderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="rx-ast-")
        cls.addClassCleanup(cls.directory.cleanup)
        cls.source = Path(cls.directory.name) / "case.rx"

    def compile(self, source):
        self.source.write_text(source)
        return subprocess.run(
            [str(COMPILER), "--stage", "semantic", str(self.source)],
            capture_output=True, text=True, timeout=10,
        )

    def check_ast(self, body, *children):
        result = self.compile("fn main() { " + body + " }")
        self.assertEqual(result.returncode, 0, result.stderr)
        expected = ("Crate", ("Function: main", ("Block", *children)))
        self.assertEqual(result.stdout, dump(expected))

    def check_expression_entries(self, expression, expected):
        entries = [
            ("ordinary", "let x = " + expression + ";", ("LetStmt: x", expected)),
            ("statement", expression + ";", ("ExprStmt:", expected)),
            ("tail", expression, expected),
            ("condition", "while " + expression + " {}",
             ("ExprStmt:", ("WhileExpr", ("Condition:", expected), ("Body:", "Block")))),
            ("condition_break", "while break " + expression + " {}",
             ("ExprStmt:", ("WhileExpr", ("Condition:", ("BreakExpr", expected)),
                            ("Body:", "Block")))),
        ]
        for entry, body, tree in entries:
            with self.subTest(expression=expression, entry=entry):
                self.check_ast(body, tree)

    def test_array_literals_in_all_entries(self):
        for expression, expected in [
            ("[]", "ArrayExpr"),
            ("[1]", ("ArrayExpr", "IntegerLiteral: 1")),
            ("[1,]", ("ArrayExpr", "IntegerLiteral: 1")),
            ("[1, 2,]", ("ArrayExpr", "IntegerLiteral: 1", "IntegerLiteral: 2")),
            ("[true, false]", ("ArrayExpr", "Boolean: true", "Boolean: false")),
            ("[1 + 2, x * 3]", ("ArrayExpr",
                ("BinaryExpr: +", "IntegerLiteral: 1", "IntegerLiteral: 2"),
                ("BinaryExpr: *", "PathExpr: x", "IntegerLiteral: 3"))),
        ]:
            self.check_expression_entries(expression, expected)

    def test_nested_arrays_in_all_entries(self):
        for expression, expected in [
            ("[[], []]", ("ArrayExpr", "ArrayExpr", "ArrayExpr")),
            ("[[1, 2], [3, 4]]", ("ArrayExpr",
                ("ArrayExpr", "IntegerLiteral: 1", "IntegerLiteral: 2"),
                ("ArrayExpr", "IntegerLiteral: 3", "IntegerLiteral: 4"))),
            ("[[0; 3]; 2]", ("ArrayRepeatExpr",
                ("Value:", ("ArrayRepeatExpr", ("Value:", "IntegerLiteral: 0"),
                            ("Count:", "IntegerLiteral: 3"))),
                ("Count:", "IntegerLiteral: 2"))),
            ("[[[false; 2]; 3]; 4]", ("ArrayRepeatExpr",
                ("Value:", ("ArrayRepeatExpr",
                    ("Value:", ("ArrayRepeatExpr", ("Value:", "Boolean: false"),
                                ("Count:", "IntegerLiteral: 2"))),
                    ("Count:", "IntegerLiteral: 3"))),
                ("Count:", "IntegerLiteral: 4"))),
            ("[[1, 2]; N]", ("ArrayRepeatExpr",
                ("Value:", ("ArrayExpr", "IntegerLiteral: 1", "IntegerLiteral: 2")),
                ("Count:", "PathExpr: N"))),
            ("[[0; N], [1; N]]", ("ArrayExpr",
                ("ArrayRepeatExpr", ("Value:", "IntegerLiteral: 0"), ("Count:", "PathExpr: N")),
                ("ArrayRepeatExpr", ("Value:", "IntegerLiteral: 1"), ("Count:", "PathExpr: N")))),
        ]:
            self.check_expression_entries(expression, expected)

    def test_repeat_counts_preserve_constant_expression(self):
        # Negative, boolean and unresolved counts remain AST nodes for later
        # semantic checks; this suite does not claim they are valid lengths.
        for count, expected in [
            ("0", "IntegerLiteral: 0"),
            ("3", "IntegerLiteral: 3"),
            ("N", "PathExpr: N"),
            ("((3))", "IntegerLiteral: 3"),
            ("((N))", "PathExpr: N"),
            ("-3", ("UnaryExpr: -", "IntegerLiteral: 3")),
            ("-N", ("UnaryExpr: -", "PathExpr: N")),
            ("-((3))", ("UnaryExpr: -", "IntegerLiteral: 3")),
            ("-((N))", ("UnaryExpr: -", "PathExpr: N")),
            ("(-((3)))", ("UnaryExpr: -", "IntegerLiteral: 3")),
            ("true", "Boolean: true"),
            ("((false))", "Boolean: false"),
            ("18446744073709551616", "IntegerLiteral: 18446744073709551616"),
        ]:
            self.check_expression_entries("[x; " + count + "]",
                ("ArrayRepeatExpr", ("Value:", "PathExpr: x"), ("Count:", expected)))

    def test_indexing_in_all_entries(self):
        for expression, expected in [
            ("a[i]", ("IndexExpr", ("Base:", "PathExpr: a"), ("Index:", "PathExpr: i"))),
            ("a[i][j]", ("IndexExpr",
                ("Base:", ("IndexExpr", ("Base:", "PathExpr: a"), ("Index:", "PathExpr: i"))),
                ("Index:", "PathExpr: j"))),
            ("[1, 2][0]", ("IndexExpr",
                ("Base:", ("ArrayExpr", "IntegerLiteral: 1", "IntegerLiteral: 2")),
                ("Index:", "IntegerLiteral: 0"))),
            ("[0; N][i]", ("IndexExpr",
                ("Base:", ("ArrayRepeatExpr", ("Value:", "IntegerLiteral: 0"),
                           ("Count:", "PathExpr: N"))),
                ("Index:", "PathExpr: i"))),
            ("a[1 + 2 * 3]", ("IndexExpr", ("Base:", "PathExpr: a"),
                ("Index:", ("BinaryExpr: +", "IntegerLiteral: 1",
                            ("BinaryExpr: *", "IntegerLiteral: 2", "IntegerLiteral: 3"))))),
            ("a[b[i]]", ("IndexExpr", ("Base:", "PathExpr: a"),
                ("Index:", ("IndexExpr", ("Base:", "PathExpr: b"), ("Index:", "PathExpr: i"))))),
            ("-a[i]", ("UnaryExpr: -",
                ("IndexExpr", ("Base:", "PathExpr: a"), ("Index:", "PathExpr: i")))),
        ]:
            self.check_expression_entries(expression, expected)

    def test_index_assignment_and_precedence(self):
        first = ("IndexExpr", ("Base:", "PathExpr: a"), ("Index:", "IntegerLiteral: 0"))
        second = ("IndexExpr", ("Base:", "PathExpr: b"), ("Index:", "IntegerLiteral: 1"))
        self.check_expression_entries("a[0] = b[1] = 2",
            ("AssignExpr", first, ("AssignExpr", second, "IntegerLiteral: 2")))
        self.check_expression_entries("a[0] + b[1] * 3 < 10",
            ("BinaryExpr: <", ("BinaryExpr: +", first,
                              ("BinaryExpr: *", second, "IntegerLiteral: 3")),
             "IntegerLiteral: 10"))
        self.check_ast("if a[0] {}", ("ExprStmt:", ("IfExpr", ("Condition:", first),
                      ("Then:", "Block"), "Else: <none>")))

    def test_integer_literals_have_consistent_support(self):
        # Keep the exact spelling, including suffixes and magnitudes too large
        # for a machine integer; semantic analysis will check their ranges.
        for literal in ["0", "00042", "0x10", "0xDeAd_BeEf", "0x_fF_",
                        "0b10", "0b_1010__", "0o10", "0o_7_0_",
                        "1_000", "1__000_", "3i32", "3u32", "3isize", "3usize",
                        "0xF_Fusize", "0b10_01u32", "0o7_7isize", "1_000_i32",
                        "0x1_0000_0000_0000_0000usize", "18446744073709551616u32"]:
            integer = "IntegerLiteral: " + literal
            for expression, expected in [
                (literal, integer),
                ("[0; " + literal + "]", ("ArrayRepeatExpr",
                    ("Value:", "IntegerLiteral: 0"), ("Count:", integer))),
                ("[0; -(" + literal + ")]", ("ArrayRepeatExpr",
                    ("Value:", "IntegerLiteral: 0"), ("Count:", ("UnaryExpr: -", integer)))),
            ]:
                with self.subTest(expression=expression):
                    self.check_ast("let x = " + expression + ";", ("LetStmt: x", expected))

    def test_integer_formats_in_all_entries(self):
        for literal in ["0xDeAd", "1__000_", "42u32", "0xFF_usize",
                        "0b_10_isize", "0o7_7i32"]:
            self.check_expression_entries(literal, "IntegerLiteral: " + literal)

        self.check_expression_entries("-0x8000_0000i32",
            ("UnaryExpr: -", "IntegerLiteral: 0x8000_0000i32"))
        self.check_expression_entries("[-0x80i32, 0b10u32, 0o7usize][0x0usize]",
            ("IndexExpr", ("Base:", ("ArrayExpr",
                ("UnaryExpr: -", "IntegerLiteral: 0x80i32"),
                "IntegerLiteral: 0b10u32", "IntegerLiteral: 0o7usize")),
             ("Index:", "IntegerLiteral: 0x0usize")))
        self.check_expression_entries("[[0xA_u32; 0b10usize]; 0o3_usize]",
            ("ArrayRepeatExpr", ("Value:", ("ArrayRepeatExpr",
                ("Value:", "IntegerLiteral: 0xA_u32"),
                ("Count:", "IntegerLiteral: 0b10usize"))),
             ("Count:", "IntegerLiteral: 0o3_usize")))

    def test_malformed_integer_literals_rejected(self):
        for literal in ["0x", "0x___", "0xG", "0b", "0b_", "0b102",
                        "0o", "0o_", "0o89", "123abc", "3u64", "3i64",
                        "3usize_extra", "0X10", "0x10u64"]:
            for expression in [literal, "[0; " + literal + "]", "[0; -(" + literal + ")]"]:
                with self.subTest(expression=expression):
                    result = self.compile("fn main() { let x = " + expression + "; }")
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertEqual(result.stdout, "")
                    self.assertIn(": error:", result.stderr)

    def test_malformed_arrays_and_indices_rejected(self):
        for expression in ["[; 3]", "[1;]", "[1;; 2]", "[1; 2,]", "[1 2]",
                           "[1; N + 1]", "[1; -true]", "[1; --3]",
                           "a[]", "a[1, 2]", "a[0", "a[;]"]:
            with self.subTest(expression=expression):
                result = self.compile("fn main() { let x = " + expression + "; }")
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertIn(": error:", result.stderr)

    def test_member_access_remains_unsupported(self):
        for expression in ["a.field", "a.method()", "a[i].field", "f().field", "f().method()"]:
            for body in ["let x = " + expression + ";", expression + ";",
                         "while " + expression + " {}", "while break " + expression + " {}"]:
                with self.subTest(expression=expression, body=body):
                    result = self.compile("fn main() { " + body + " }")
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertIn("not supported yet", result.stderr)

    def test_precedence_and_associativity_in_all_entries(self):
        cases = [
            ("1", "IntegerLiteral: 1"),
            ("x", "PathExpr: x"),
            ("1 + 2 * 3", ("BinaryExpr: +", "IntegerLiteral: 1",
                            ("BinaryExpr: *", "IntegerLiteral: 2", "IntegerLiteral: 3"))),
            ("8 - 3 - 1", ("BinaryExpr: -",
                            ("BinaryExpr: -", "IntegerLiteral: 8", "IntegerLiteral: 3"),
                            "IntegerLiteral: 1")),
            ("24 / 3 / 2", ("BinaryExpr: /",
                             ("BinaryExpr: /", "IntegerLiteral: 24", "IntegerLiteral: 3"),
                             "IntegerLiteral: 2")),
            ("1 + 2 + 3 * 4 * 5 < 100", ("BinaryExpr: <",
                ("BinaryExpr: +", ("BinaryExpr: +", "IntegerLiteral: 1", "IntegerLiteral: 2"),
                 ("BinaryExpr: *", ("BinaryExpr: *", "IntegerLiteral: 3", "IntegerLiteral: 4"),
                  "IntegerLiteral: 5")), "IntegerLiteral: 100")),
            ("1 <= 2", ("BinaryExpr: <=", "IntegerLiteral: 1", "IntegerLiteral: 2")),
            ("true || false && true", ("BinaryExpr: ||", "Boolean: true",
                                       ("BinaryExpr: &&", "Boolean: false", "Boolean: true"))),
            ("true || false || true", ("BinaryExpr: ||",
                                       ("BinaryExpr: ||", "Boolean: true", "Boolean: false"),
                                       "Boolean: true")),
            ("x = y = 1", ("AssignExpr", "PathExpr: x",
                           ("AssignExpr", "PathExpr: y", "IntegerLiteral: 1"))),
            ("-(1 + 2)", ("UnaryExpr: -",
                           ("BinaryExpr: +", "IntegerLiteral: 1", "IntegerLiteral: 2"))),
        ]
        for expression, expected in cases:
            entries = [
                ("ordinary", "let x = " + expression + ";", ("LetStmt: x", expected)),
                ("statement", expression + ";", ("ExprStmt:", expected)),
                ("tail", expression, expected),
                ("condition", "while " + expression + " {}",
                 ("ExprStmt:", ("WhileExpr", ("Condition:", expected), ("Body:", "Block")))),
                ("condition_break", "while break " + expression + " {}",
                 ("ExprStmt:", ("WhileExpr", ("Condition:", ("BreakExpr", expected)),
                                ("Body:", "Block")))),
            ]
            for entry, body, tree in entries:
                with self.subTest(expression=expression, entry=entry):
                    self.check_ast(body, tree)

    def test_comparison_operators(self):
        for operator in ["<", "<=", ">", ">=", "==", "!="]:
            with self.subTest(operator=operator):
                expected = ("BinaryExpr: " + operator, "IntegerLiteral: 1", "IntegerLiteral: 2")
                self.check_ast("1 " + operator + " 2;", ("ExprStmt:", expected))

    def test_break_block_boundary(self):
        self.check_ast("while break { continue; }",
            ("ExprStmt:", ("WhileExpr", ("Condition:", ("BreakExpr", "<no value>")),
                           ("Body:", ("Block", ("ExprStmt:", "ContinueExpr"))))))
        self.check_ast("if break {}",
            ("ExprStmt:", ("IfExpr", ("Condition:", ("BreakExpr", "<no value>")),
                           ("Then:", "Block"), "Else: <none>")))
        for operand, value in [
            ("({ 1 })", ("Block", "IntegerLiteral: 1")),
            ("-{ 1 }", ("UnaryExpr: -", ("Block", "IntegerLiteral: 1"))),
            ("1 + { 2 }", ("BinaryExpr: +", "IntegerLiteral: 1", ("Block", "IntegerLiteral: 2"))),
        ]:
            with self.subTest(operand=operand):
                self.check_ast("while break " + operand + " {}",
                    ("ExprStmt:", ("WhileExpr", ("Condition:", ("BreakExpr", value)),
                                   ("Body:", "Block"))))

    def test_block_expression_dispatch(self):
        expressions = [
            ("{ 1 }", ("Block", "IntegerLiteral: 1")),
            ("if true { 1 } else { 2 }", ("IfExpr", ("Condition:", "Boolean: true"),
                ("Then:", ("Block", "IntegerLiteral: 1")), ("Else:", ("Block", "IntegerLiteral: 2")))),
            ("while false {}", ("WhileExpr", ("Condition:", "Boolean: false"), ("Body:", "Block"))),
            ("loop { break 1; }", ("LoopExpr", ("Body:", ("Block",
                ("ExprStmt:", ("BreakExpr", "IntegerLiteral: 1")))))),
        ]
        for expression, expected in expressions:
            with self.subTest(expression=expression):
                self.check_ast(expression, ("ExprStmt:", expected))
                self.check_ast("let x = " + expression + ";", ("LetStmt: x", expected))

if __name__ == "__main__":
    unittest.main()
