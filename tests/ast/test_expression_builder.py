#!/usr/bin/env python3
"""Check AST structure only; loop ownership and types belong to semantic analysis.

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

    def test_unsupported_operations_remain_rejected(self):
        for expression, diagnostic in [
            ("1 as i32", "as casts are not supported yet"),
            ("1 as (i32) < 2", "as casts are not supported yet"),
            ("x += 1", "compound assignment is not supported yet"),
            ("1 | 2", "this expression form is not supported yet"),
            ("1 << 2", "this expression form is not supported yet"),
        ]:
            for body in ["let x = " + expression + ";", expression + ";",
                         "while " + expression + " {}", "while break " + expression + " {}"]:
                with self.subTest(expression=expression, body=body):
                    result = self.compile("fn main() { " + body + " }")
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertEqual(result.stderr, "error: " + diagnostic + "\n")


if __name__ == "__main__":
    unittest.main()
