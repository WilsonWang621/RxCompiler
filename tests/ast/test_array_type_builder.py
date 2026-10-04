#!/usr/bin/env python3
"""Check array types and annotations; size evaluation and type matching come later.

Run all AST suites: python3 -m unittest discover -s tests/ast -p 'test_*builder.py'
"""

from pathlib import Path
import subprocess
import tempfile
import unittest

from test_expression_builder import COMPILER, dump


def array_type(element, count):
    return ("ArrayTypeRef", ("ElementType:", element), ("Count:", count))


def repeat(value, count):
    return ("ArrayRepeatExpr", ("Value:", value), ("Count:", count))


class ArrayTypeBuilderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="rx-array-type-ast-")
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

    def check_let(self, type_source, initializer, type_tree, value_tree):
        self.check_ast("fn main() { let a: " + type_source + " = " + initializer + "; }",
            ("Function: main", ("Block", ("LetStmt: a", ("Type:", type_tree), value_tree))))

    def test_array_types_in_let_annotations(self):
        int_array = array_type("TypeRef: i32", "IntegerLiteral: 3")
        zeros = repeat("IntegerLiteral: 0", "IntegerLiteral: 3")
        for type_source in ["[i32; 3]", "[(i32); ((3))]", "([i32; 3])"]:
            with self.subTest(type_source=type_source):
                self.check_let(type_source, "[0; 3]", int_array, zeros)
        self.check_let("[bool; 0]", "[]",
            array_type("TypeRef: bool", "IntegerLiteral: 0"), "ArrayExpr")
        self.check_let("[i32; N]", "[0; N]",
            array_type("TypeRef: i32", "PathExpr: N"), repeat("IntegerLiteral: 0", "PathExpr: N"))

    def test_nested_array_types_and_values(self):
        self.check_let("[[i32; 3]; 2]", "[[0; 3]; 2]",
            array_type(array_type("TypeRef: i32", "IntegerLiteral: 3"), "IntegerLiteral: 2"),
            repeat(repeat("IntegerLiteral: 0", "IntegerLiteral: 3"), "IntegerLiteral: 2"))
        self.check_let("[[[bool; 2]; 3]; 4]", "[[[false; 2]; 3]; 4]",
            array_type(array_type(array_type("TypeRef: bool", "IntegerLiteral: 2"),
                                  "IntegerLiteral: 3"), "IntegerLiteral: 4"),
            repeat(repeat(repeat("Boolean: false", "IntegerLiteral: 2"),
                          "IntegerLiteral: 3"), "IntegerLiteral: 4"))

    def test_array_parameters_and_return_type(self):
        row = array_type("TypeRef: i32", "IntegerLiteral: 3")
        matrix = array_type(row, "IntegerLiteral: 2")
        flags = array_type("TypeRef: bool", "PathExpr: N")
        self.check_ast("fn row(mut a: [[i32; 3]; 2], flags: [bool; N]) -> ([i32; 3]) { a[0] }",
            ("Function: row", ("Parameter: mut a", matrix), ("Parameter: flags", flags),
             ("ReturnType", row),
             ("Block", ("IndexExpr", ("Base:", "PathExpr: a"), ("Index:", "IntegerLiteral: 0")))))

    def test_array_type_in_constant_declarations(self):
        # The grammar allows a constant path as the value; lookup is deferred.
        row = array_type("TypeRef: i32", "PathExpr: N")
        self.check_ast("const N: usize = 0x3usize; const A: [i32; N] = SOURCE; "
                       "const B: [[i32; N]; 2] = MATRIX;",
            ("ConstItem: N", ("Type:", "TypeRef: usize"), ("Value:", "IntegerLiteral: 0x3usize")),
            ("ConstItem: A", ("Type:", row), ("Value:", "PathExpr: SOURCE")),
            ("ConstItem: B", ("Type:", array_type(row, "IntegerLiteral: 2")),
             ("Value:", "PathExpr: MATRIX")))

    def test_array_lengths_preserve_constant_expressions(self):
        # These assertions concern AST structure, not whether a length is valid.
        for source, expected in [
            ("3", "IntegerLiteral: 3"), ("((3))", "IntegerLiteral: 3"),
            ("N", "PathExpr: N"), ("((N))", "PathExpr: N"),
            ("-3", ("UnaryExpr: -", "IntegerLiteral: 3")),
            ("-((N))", ("UnaryExpr: -", "PathExpr: N")),
            ("true", "Boolean: true"), ("((false))", "Boolean: false"),
            ("0x10_usize", "IntegerLiteral: 0x10_usize"),
            ("18446744073709551616usize", "IntegerLiteral: 18446744073709551616usize"),
        ]:
            with self.subTest(source=source):
                self.check_ast("fn f(a: [i32; " + source + "]) {}",
                    ("Function: f", ("Parameter: a", array_type("TypeRef: i32", expected)), "Block"))

    def test_parenthesized_and_unit_element_types(self):
        self.check_ast("fn f(x: (i32), y: ((())), z: [(); 0]) -> () {}",
            ("Function: f", ("Parameter: x", "TypeRef: i32"), ("Parameter: y", "TypeRef: ()"),
             ("Parameter: z", array_type("TypeRef: ()", "IntegerLiteral: 0")),
             ("ReturnType", "TypeRef: ()"), "Block"))

    def test_optional_annotations_and_index_assignment(self):
        matrix = array_type(array_type("TypeRef: i32", "IntegerLiteral: 3"), "IntegerLiteral: 2")
        zeros = repeat(repeat("IntegerLiteral: 0", "IntegerLiteral: 3"), "IntegerLiteral: 2")
        target = ("IndexExpr", ("Base:", ("IndexExpr", ("Base:", "PathExpr: m"),
                  ("Index:", "IntegerLiteral: 1"))), ("Index:", "IntegerLiteral: 2"))
        first = ("IndexExpr", ("Base:", ("IndexExpr", ("Base:", "PathExpr: m"),
                 ("Index:", "IntegerLiteral: 0"))), ("Index:", "IntegerLiteral: 0"))
        self.check_ast("fn main() { let x = 1; let mut m: ([[i32; 3]; 2]) = [[0; 3]; 2]; "
                       "m[1][2] = 7; m[0][0] }",
            ("Function: main", ("Block", ("LetStmt: x", "IntegerLiteral: 1"),
             ("LetStmt: mut m", ("Type:", matrix), zeros),
             ("ExprStmt:", ("AssignExpr", target, "IntegerLiteral: 7")), first)))

    def test_scalar_annotations(self):
        self.check_let("i32", "1", "TypeRef: i32", "IntegerLiteral: 1")
        self.check_let("((bool))", "true", "TypeRef: bool", "Boolean: true")

    def test_malformed_array_types_rejected(self):
        for type_source in ["[i32]", "[i32;]", "[; 3]", "[i32; 3,]", "[i32; 3 + 1]",
                            "[i32; -true]", "[i32; --3]", "[i32; 0xG]", "[i32; 3u64]",
                            "[]", "[[i32; 3];]"]:
            with self.subTest(type_source=type_source):
                result = self.compile("fn f(a: " + type_source + ") {}")
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertIn(": error:", result.stderr)


if __name__ == "__main__":
    unittest.main()
