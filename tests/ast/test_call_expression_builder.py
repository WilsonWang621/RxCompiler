#!/usr/bin/env python3
"""Check call ASTs; function lookup and argument type checking come later.

Run all AST suites: python3 -m unittest discover -s tests/ast -p 'test_*builder.py'
"""

from pathlib import Path
import subprocess
import tempfile
import unittest

from test_expression_builder import COMPILER, dump


def call(callee, *arguments):
    return ("CallExpr", ("Callee:", callee), ("Arguments:", *arguments))


def index(base, subscript):
    return ("IndexExpr", ("Base:", base), ("Index:", subscript))


class CallExpressionBuilderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="rx-call-ast-")
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
        self.assertEqual(result.stdout, dump(("Crate", ("Function: main", ("Block", *children)))))

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

    def test_empty_arguments_in_all_entries(self):
        self.check_expression_entries("f()", call("PathExpr: f"))

    def test_argument_order_and_trailing_comma(self):
        for expression, expected in [
            ("f(1)", call("PathExpr: f", "IntegerLiteral: 1")),
            ("f(1,)", call("PathExpr: f", "IntegerLiteral: 1")),
            ("f(1, x, false)", call("PathExpr: f", "IntegerLiteral: 1", "PathExpr: x", "Boolean: false")),
            ("f(1, x, false,)", call("PathExpr: f", "IntegerLiteral: 1", "PathExpr: x", "Boolean: false")),
        ]:
            self.check_expression_entries(expression, expected)

    def test_nested_calls_preserve_argument_boundaries(self):
        self.check_expression_entries("f(g(1, 2), h(), k(3, 4,))",
            call("PathExpr: f", call("PathExpr: g", "IntegerLiteral: 1", "IntegerLiteral: 2"),
                 call("PathExpr: h"), call("PathExpr: k", "IntegerLiteral: 3", "IntegerLiteral: 4")))

    def test_chained_calls_associate_left(self):
        self.check_expression_entries("f()(x)(1, 2)",
            call(call(call("PathExpr: f"), "PathExpr: x"), "IntegerLiteral: 1", "IntegerLiteral: 2"))

    def test_calls_and_indices_follow_source_order(self):
        for expression, expected in [
            ("a[i]()", call(index("PathExpr: a", "PathExpr: i"))),
            ("f()[i](x)", call(index(call("PathExpr: f"), "PathExpr: i"), "PathExpr: x")),
            ("a[i](x)[j]", index(call(index("PathExpr: a", "PathExpr: i"), "PathExpr: x"), "PathExpr: j")),
        ]:
            self.check_expression_entries(expression, expected)

    def test_callee_is_an_expression(self):
        # Whether each callee is callable is a later semantic check.
        for expression, expected in [
            ("((f))(x)", call("PathExpr: f", "PathExpr: x")),
            ("(f + g)(x)", call(("BinaryExpr: +", "PathExpr: f", "PathExpr: g"), "PathExpr: x")),
            ("({ f })(x)", call(("Block", "PathExpr: f"), "PathExpr: x")),
            ("1(2)", call("IntegerLiteral: 1", "IntegerLiteral: 2")),
        ]:
            self.check_expression_entries(expression, expected)

    def test_call_and_argument_precedence(self):
        self.check_expression_entries("-f(1 + 2 * 3, x = y) + g() * 4 < h()",
            ("BinaryExpr: <", ("BinaryExpr: +",
                ("UnaryExpr: -", call("PathExpr: f",
                    ("BinaryExpr: +", "IntegerLiteral: 1",
                        ("BinaryExpr: *", "IntegerLiteral: 2", "IntegerLiteral: 3")),
                    ("AssignExpr", "PathExpr: x", "PathExpr: y"))),
                ("BinaryExpr: *", call("PathExpr: g"), "IntegerLiteral: 4")), call("PathExpr: h")))

    def test_arguments_allow_blocks_at_condition_boundaries(self):
        # Argument parentheses delimit ordinary expressions even in conditions.
        self.check_expression_entries("f({ 1 }, if true { 2 } else { 3 })",
            call("PathExpr: f", ("Block", "IntegerLiteral: 1"),
                 ("IfExpr", ("Condition:", "Boolean: true"),
                  ("Then:", ("Block", "IntegerLiteral: 2")),
                  ("Else:", ("Block", "IntegerLiteral: 3")))))

    def test_calls_in_arrays_indices_and_control_flow(self):
        self.check_expression_entries("f([g(), h()])[next()]",
            index(call("PathExpr: f", ("ArrayExpr", call("PathExpr: g"), call("PathExpr: h"))),
                  call("PathExpr: next")))
        self.check_ast("return f(x);", ("ExprStmt:", ("ReturnExpr", call("PathExpr: f", "PathExpr: x"))))
        self.check_ast("loop { break f(x); }",
            ("ExprStmt:", ("LoopExpr", ("Body:",
                ("Block", ("ExprStmt:", ("BreakExpr", call("PathExpr: f", "PathExpr: x"))))))))
        self.check_ast("if f(x) { g(); } else { h(); }",
            ("ExprStmt:", ("IfExpr", ("Condition:", call("PathExpr: f", "PathExpr: x")),
                ("Then:", ("Block", ("ExprStmt:", call("PathExpr: g")))),
                ("Else:", ("Block", ("ExprStmt:", call("PathExpr: h")))))))

    def test_calls_between_function_items(self):
        result = self.compile(
            "fn add(x: i32, y: i32) -> i32 { x + y } "
            "fn main() { let n = add(1, 2); add(n, 3) }"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, dump(("Crate",
            ("Function: add", ("Parameter: x", "TypeRef: i32"),
                ("Parameter: y", "TypeRef: i32"), ("ReturnType", "TypeRef: i32"),
                ("Block", ("BinaryExpr: +", "PathExpr: x", "PathExpr: y"))),
            ("Function: main", ("Block",
                ("LetStmt: n", call("PathExpr: add", "IntegerLiteral: 1", "IntegerLiteral: 2")),
                call("PathExpr: add", "PathExpr: n", "IntegerLiteral: 3"))))))

    def test_malformed_calls_rejected(self):
        for expression in ["f(,)", "f(,1)", "f(1,,2)", "f(1,,)", "f(1 2)",
                           "f(1; 2)", "f(", "f(1", "f(1))", "f()(,)"]:
            with self.subTest(expression=expression):
                result = self.compile("fn main() { let x = " + expression + "; }")
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertIn(": error:", result.stderr)


if __name__ == "__main__":
    unittest.main()
