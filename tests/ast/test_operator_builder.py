#!/usr/bin/env python3
"""Check operator AST structure; operand types and place validity come later.

Run all AST suites: python3 -m unittest discover -s tests/ast -p 'test_*builder.py'
"""

from pathlib import Path
import subprocess
import tempfile
import unittest

from test_expression_builder import COMPILER, dump


def path(name):
    return "PathExpr: " + name


def binary(operator, left, right):
    return ("BinaryExpr: " + operator, left, right)


def unary(operator, operand):
    return ("UnaryExpr: " + operator, operand)


def cast(value, target):
    return ("CastExpr", ("Value:", value), ("Type:", target))


def reference(target, mutable=False, lifetime=None):
    label = "ReferenceTypeRef: &" + (lifetime or "")
    if mutable:
        label += " mut" if lifetime else "mut"
    return (label, target)


def call(callee, *arguments):
    return ("CallExpr", ("Callee:", callee), ("Arguments:", *arguments))


def index(base, subscript):
    return ("IndexExpr", ("Base:", base), ("Index:", subscript))


class OperatorBuilderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="rx-operator-ast-")
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
        self.assertEqual(result.stderr, "")
        self.assertEqual(result.stdout, dump(("Crate", ("Function: main", ("Block", *children)))))

    def check_expression_entries(self, expression, expected):
        entries = [
            ("ordinary", "let value = " + expression + ";", ("LetStmt: value", expected)),
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

    def test_all_binary_operators_in_all_entries(self):
        for operator in ["+", "-", "*", "/", "%", "&", "^", "|", "<<", ">>",
                         "==", "!=", "<", "<=", ">", ">=", "&&", "||"]:
            self.check_expression_entries("a " + operator + " b", binary(operator, path("a"), path("b")))

    def test_unary_operators_and_double_borrows(self):
        for expression, expected in [
            ("-x", unary("-", path("x"))),
            ("!x", unary("!", path("x"))),
            ("*x", unary("*", path("x"))),
            ("&x", unary("&", path("x"))),
            ("&mut x", unary("&mut", path("x"))),
            ("&&x", unary("&", unary("&", path("x")))),
            ("&&mut x", unary("&", unary("&mut", path("x")))),
            ("&mut &x", unary("&mut", unary("&", path("x")))),
            ("*&mut x", unary("*", unary("&mut", path("x")))),
            ("!-*x", unary("!", unary("-", unary("*", path("x"))))),
        ]:
            self.check_expression_entries(expression, expected)

    def test_all_compound_assignments_in_all_entries(self):
        for operator in ["+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="]:
            self.check_expression_entries("x " + operator + " y + 1",
                ("CompoundAssignExpr: " + operator, path("x"),
                 binary("+", path("y"), "IntegerLiteral: 1")))

    def test_compound_assignment_preserves_target_once(self):
        target = index(call(path("values")), call(path("next")))
        self.check_expression_entries("values()[next()] += f()",
            ("CompoundAssignExpr: +=", target, call(path("f"))))
        self.check_expression_entries("*p >>= 1", ("CompoundAssignExpr: >>=",
            unary("*", path("p")), "IntegerLiteral: 1"))

    def test_assignment_is_right_associative(self):
        for expression, expected in [
            ("a += b *= c", ("CompoundAssignExpr: +=", path("a"),
                             ("CompoundAssignExpr: *=", path("b"), path("c")))),
            ("a = b <<= c", ("AssignExpr", path("a"),
                            ("CompoundAssignExpr: <<=", path("b"), path("c")))),
            ("a >>= b = c", ("CompoundAssignExpr: >>=", path("a"),
                            ("AssignExpr", path("b"), path("c")))),
        ]:
            self.check_expression_entries(expression, expected)

    def test_binary_chains_are_left_associative(self):
        for operator in ["+", "-", "*", "/", "%", "&", "^", "|", "<<", ">>", "&&", "||"]:
            self.check_expression_entries("a " + operator + " b " + operator + " c",
                binary(operator, binary(operator, path("a"), path("b")), path("c")))

    def test_mixed_shift_order_and_additive_precedence(self):
        for expression, expected in [
            ("a << b >> c", binary(">>", binary("<<", path("a"), path("b")), path("c"))),
            ("a >> b << c", binary("<<", binary(">>", path("a"), path("b")), path("c"))),
            ("a << b >> c << d", binary("<<",
                binary(">>", binary("<<", path("a"), path("b")), path("c")), path("d"))),
            ("a + b << c * d >> e - f", binary(">>",
                binary("<<", binary("+", path("a"), path("b")), binary("*", path("c"), path("d"))),
                binary("-", path("e"), path("f")))),
        ]:
            self.check_expression_entries(expression, expected)

    def test_full_precedence_ladder(self):
        # Each successive operator binds more tightly than the preceding one.
        expected = unary("-", path("k"))
        for operator, left in [("*", "j"), ("+", "i"), ("<<", "h"), ("&", "g"),
                               ("^", "f"), ("|", "e"), ("==", "d"), ("&&", "c"), ("||", "b")]:
            expected = binary(operator, path(left), expected)
        self.check_expression_entries("a = b || c && d == e | f ^ g & h << i + j * -k",
            ("AssignExpr", path("a"), expected))

    def test_closed_bit_chains_before_less_than(self):
        for operator in ["|", "^", "&"]:
            self.check_expression_entries("a " + operator + " b " + operator + " c as (i32) < d",
                binary("<", binary(operator, binary(operator, path("a"), path("b")),
                    cast(path("c"), "TypeRef: i32")), path("d")))
        self.check_expression_entries("a | b ^ c & d >> e as (i32) << f < g",
            binary("<", binary("|", path("a"), binary("^", path("b"),
                binary("&", path("c"), binary("<<",
                    binary(">>", path("d"), cast(path("e"), "TypeRef: i32")), path("f"))))), path("g")))

    def test_cast_chain_and_precedence(self):
        for expression, expected in [
            ("x as i32", cast(path("x"), "TypeRef: i32")),
            ("x as i32 as usize", cast(cast(path("x"), "TypeRef: i32"), "TypeRef: usize")),
            ("-x as i32", cast(unary("-", path("x")), "TypeRef: i32")),
            ("-(x as i32)", unary("-", cast(path("x"), "TypeRef: i32"))),
            ("x as i32 * y + z", binary("+",
                binary("*", cast(path("x"), "TypeRef: i32"), path("y")), path("z"))),
            ("x as i32 as (usize) << y", binary("<<",
                cast(cast(path("x"), "TypeRef: i32"), "TypeRef: usize"), path("y"))),
            ("x as (i32) < y", binary("<", cast(path("x"), "TypeRef: i32"), path("y"))),
            ("a + b * c as (i32) < d", binary("<", binary("+", path("a"),
                binary("*", path("b"), cast(path("c"), "TypeRef: i32"))), path("d"))),
        ]:
            self.check_expression_entries(expression, expected)

    def test_cast_target_type_structure(self):
        array = ("ArrayTypeRef", ("ElementType:", "TypeRef: i32"), ("Count:", "IntegerLiteral: 3"))
        for target, expected in [
            ("()", "TypeRef: ()"),
            ("((i32))", "TypeRef: i32"),
            ("[i32; 3]", array),
            ("&i32", reference("TypeRef: i32")),
            ("&mut [i32; 3]", reference(array, mutable=True)),
            ("&&'a mut i32", reference(reference("TypeRef: i32", True, "'a"))),
            ("&&'a mut (i32)", reference(reference("TypeRef: i32", True, "'a"))),
            ("pkg::T<'a>", "TypeRef: pkg::T<'a>"),
            ("Outer<Inner<'a>>", "TypeRef: Outer<Inner<'a>>"),
        ]:
            self.check_expression_entries("x as " + target, cast(path("x"), expected))
        for target, expected in [
            ("[i32; 3]", array),
            ("&&'a mut (i32)", reference(reference("TypeRef: i32", True, "'a"))),
            ("pkg::T<'a>", "TypeRef: pkg::T<'a>"),
            ("Outer<Inner<'a>>", "TypeRef: Outer<Inner<'a>>"),
        ]:
            self.check_expression_entries("x as " + target + " < y",
                binary("<", cast(path("x"), expected), path("y")))
            self.check_expression_entries("x as " + target + " << y",
                binary("<<", cast(path("x"), expected), path("y")))

    def test_reference_types_in_annotations_and_items(self):
        result = self.compile(
            "const R: &i32 = OTHER; "
            "fn borrow(x: &&'a mut i32) -> &'a mut i32 { *x } "
            "fn main() { let r: &mut [i32; 3] = &mut a; }"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        array = ("ArrayTypeRef", ("ElementType:", "TypeRef: i32"), ("Count:", "IntegerLiteral: 3"))
        self.assertEqual(result.stdout, dump(("Crate",
            ("ConstItem: R", ("Type:", reference("TypeRef: i32")), ("Value:", path("OTHER"))),
            ("Function: borrow", ("Parameter: x", reference(reference("TypeRef: i32", True, "'a"))),
                ("ReturnType", reference("TypeRef: i32", True, "'a")), ("Block", unary("*", path("x")))),
            ("Function: main", ("Block", ("LetStmt: r", ("Type:", reference(array, True)),
                                         unary("&mut", path("a"))))))))

    def test_operators_in_call_arguments_and_arrays(self):
        self.check_expression_entries("f(&mut a[i], x | y, z as i32, t += 1)",
            call(path("f"), unary("&mut", index(path("a"), path("i"))),
                binary("|", path("x"), path("y")), cast(path("z"), "TypeRef: i32"),
                ("CompoundAssignExpr: +=", path("t"), "IntegerLiteral: 1")))
        self.check_expression_entries("[a << 1, b as i32][i & 3]",
            index(("ArrayExpr", binary("<<", path("a"), "IntegerLiteral: 1"),
                   cast(path("b"), "TypeRef: i32")), binary("&", path("i"), "IntegerLiteral: 3")))
        self.check_expression_entries("*f(x)[i] as i32", cast(
            unary("*", index(call(path("f"), path("x")), path("i"))), "TypeRef: i32"))

    def test_condition_block_boundaries(self):
        for expression, expected in [
            ("x | { y }", binary("|", path("x"), ("Block", path("y")))),
            ("x << { y }", binary("<<", path("x"), ("Block", path("y")))),
            ("&mut { x }", unary("&mut", ("Block", path("x")))),
        ]:
            self.check_expression_entries(expression, expected)
        condition = binary("!=", binary("&", path("x"), "IntegerLiteral: 1"), "IntegerLiteral: 0")
        self.check_ast("if x & 1 != 0 {}", ("ExprStmt:", ("IfExpr", ("Condition:", condition),
                       ("Then:", "Block"), "Else: <none>")))

    def test_break_operand_includes_the_shift_chain(self):
        # The parser must consume the shift as part of break's operand.
        operand = binary("<<", binary(">>", path("a"), path("b")), path("c"))
        self.check_ast("if break a >> b << c {}",
            ("ExprStmt:", ("IfExpr", ("Condition:", ("BreakExpr", operand)),
                           ("Then:", "Block"), "Else: <none>")))
        self.check_ast("while break a >> b << c < d {}",
            ("ExprStmt:", ("WhileExpr", ("Condition:", ("BreakExpr", binary("<", operand, path("d")))),
                           ("Body:", "Block"))))

    def test_shift_operands_in_return_and_break(self):
        operand = binary("<<", binary(">>", path("a"), path("b")), path("c"))
        self.check_ast("return a >> b << c;", ("ExprStmt:", ("ReturnExpr", operand)))
        self.check_ast("loop { break a >> b << c; }",
            ("ExprStmt:", ("LoopExpr", ("Body:", ("Block", ("ExprStmt:", ("BreakExpr", operand)))))))
        self.check_ast("let x = break a >> b << c;", ("LetStmt: x", ("BreakExpr", operand)))
        self.check_ast("while return a >> b << c {}",
            ("ExprStmt:", ("WhileExpr", ("Condition:", ("ReturnExpr", operand)), ("Body:", "Block"))))

    def test_semantic_operand_checks_are_deferred(self):
        # These trees may be semantically invalid; the AST builder preserves them.
        for expression, expected in [
            ("1 += 2", ("CompoundAssignExpr: +=", "IntegerLiteral: 1", "IntegerLiteral: 2")),
            ("true << false", binary("<<", "Boolean: true", "Boolean: false")),
            ("true as [i32; 3]", cast("Boolean: true", ("ArrayTypeRef",
                ("ElementType:", "TypeRef: i32"), ("Count:", "IntegerLiteral: 3")))),
        ]:
            self.check_expression_entries(expression, expected)

    def test_malformed_operators_remain_syntax_errors(self):
        for expression in ["a |", "a ^", "a <<", "a >>", "a +=", "a <<=", "a >>=", "x as",
                           "&mut", "*", "a < b < c", "a == b == c", "a > > b", "a < < b",
                           "a >/*gap*/> b", "a > >= b", "a + = b", "+x", "x as i32 < y"]:
            with self.subTest(expression=expression):
                result = self.compile("fn main() { let value = " + expression + "; }")
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertIn(": error:", result.stderr)


if __name__ == "__main__":
    unittest.main()
