#!/usr/bin/env python3
"""Fixtures for .github/scripts/check-cpp-style.py."""

from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile
import textwrap

CHECKER = pathlib.Path(__file__).with_name("check-cpp-style.py")

BEFORE = "missing blank line before"
AFTER = "missing blank line after"
ADD_BRACES = "missing braces on this branch"
ADD_LOOP_BRACES = "missing braces around this"
REMOVE_BRACES = "unnecessary braces around this single-statement"
NS_OPEN = "blank line after the namespace opening brace"
NS_CLOSE = "blank line before the namespace closing brace"
SEMICOLON = "redundant semicolon"
FOREVER = "rewrite this 'for (;;)'"
CASE_BLANK = "this case label"
CHECK_BLANK = "remove the blank line before this check"
GROUP_BLANK = "blank line inside this declaration/assignment group"


def run(root: pathlib.Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(CHECKER), "--root", str(root), *args],
        capture_output=True,
        text=True,
    )


def write(root: pathlib.Path, name: str, text: str) -> pathlib.Path:
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(text).lstrip("\n"), encoding="utf-8")
    return path


def expect(
    root: pathlib.Path,
    name: str,
    text: str,
    *,
    before: int = 0,
    after: int = 0,
    add: int = 0,
    remove: int = 0,
    loop_add: int = 0,
    ns_open: int = 0,
    ns_close: int = 0,
    semicolons: int = 0,
    forever: int = 0,
    case_blank: int = 0,
    checks: int = 0,
    groups: int = 0,
) -> pathlib.Path:
    path = write(root, name, text)
    result = run(root, str(path))
    expected = (
        before
        + after
        + add
        + remove
        + loop_add
        + ns_open
        + ns_close
        + semicolons
        + forever
        + case_blank
        + checks
        + groups
    )
    assert result.returncode == (1 if expected else 0), (
        name,
        result.stdout,
        result.stderr,
    )
    assert result.stdout.count(BEFORE) == before, (name, result.stdout)
    assert result.stdout.count(AFTER) == after, (name, result.stdout)
    assert result.stdout.count(ADD_BRACES) == add, (name, result.stdout)
    assert result.stdout.count(ADD_LOOP_BRACES) == loop_add, (name, result.stdout)
    assert result.stdout.count(REMOVE_BRACES) == remove, (name, result.stdout)
    assert result.stdout.count(NS_OPEN) == ns_open, (name, result.stdout)
    assert result.stdout.count(NS_CLOSE) == ns_close, (name, result.stdout)
    assert result.stdout.count(SEMICOLON) == semicolons, (name, result.stdout)
    assert result.stdout.count(FOREVER) == forever, (name, result.stdout)
    assert result.stdout.count(CASE_BLANK) == case_blank, (name, result.stdout)
    assert result.stdout.count(CHECK_BLANK) == checks, (name, result.stdout)
    assert result.stdout.count(GROUP_BLANK) == groups, (name, result.stdout)
    return path


def git(root: pathlib.Path, *args: str) -> str:
    return subprocess.run(
        ["git", "-C", str(root), *args],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()


def test_clean(root: pathlib.Path) -> None:
    expect(
        root,
        "clean.cpp",
        """
        void f()
        {
            run();

            if (a)
                work();

            stop();
        }
        """,
    )


def test_first_and_last_statement(root: pathlib.Path) -> None:
    expect(
        root,
        "boundaries.cpp",
        """
        void f()
        {
            if (a)
                work();
        }
        """,
    )


def test_blank_lines_both_sides(root: pathlib.Path) -> None:
    expect(
        root,
        "both.cpp",
        """
        void f()
        {
            run();
            if (a) return;
            stop();
        }
        """,
        before=1,
        after=1,
    )


def test_else_chain_blank_lines(root: pathlib.Path) -> None:
    expect(
        root,
        "chain.cpp",
        """
        void f()
        {
            run();

            if (a)
                first();
            else if (b)
                second();
            else
                third();

            stop();
        }
        """,
    )


def test_loops_keep_braces(root: pathlib.Path) -> None:
    expect(
        root,
        "loops.cpp",
        """
        void f()
        {
            run();

            for (int i = 0; i < n; ++i) {
                step();
            }

            while (again()) {
                step();
            }

            do {
                step();
            } while (again());

            stop();
        }
        """,
    )


def test_for_requires_braces(root: pathlib.Path) -> None:
    expect(
        root,
        "for-braces.cpp",
        """
        void f()
        {
            run();

            for (int i = 0; i < n; ++i)
                step();

            stop();
        }
        """,
        loop_add=1,
    )


def test_while_requires_braces(root: pathlib.Path) -> None:
    expect(
        root,
        "while-braces.cpp",
        """
        void f()
        {
            run();

            while (running)
                step();

            stop();
        }
        """,
        loop_add=1,
    )


def test_nested_braceless_loop_gets_braces(root: pathlib.Path) -> None:
    expect(
        root,
        "loop-nested.cpp",
        """
        void f()
        {
            if (x)
                for (int i = 0; i < n; ++i)
                    step();
        }
        """,
        loop_add=1,
    )


def test_do_requires_braces(root: pathlib.Path) -> None:
    expect(
        root,
        "do-body.cpp",
        """
        void f()
        {
            run();

            do
                step();
            while (running);

            stop();
        }
        """,
        loop_add=1,
    )


def test_qualified_call_loop_body_gets_braces(root: pathlib.Path) -> None:
    path = write(
        root,
        "qualified-loop.cpp",
        """
        void f()
        {
            run();

            while (state == 1)
                astia::arch::cpu::relax();

            stop();
        }
        """,
    )
    result = run(root, str(path))
    assert result.returncode == 1, (result.stdout, result.stderr)
    assert result.stdout.count(ADD_LOOP_BRACES) == 1, result.stdout
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    text = path.read_text(encoding="utf-8")
    assert "{astia:}" not in text
    assert text == textwrap.dedent(
        """\
        void f()
        {
            run();

            while (state == 1) {
                astia::arch::cpu::relax();
            }

            stop();
        }
        """
    )


def test_fix_for_braces(root: pathlib.Path) -> None:
    path = write(
        root,
        "fix-for-braces.cpp",
        """
        void f()
        {
            run();

            for (int i = 0; i < n; ++i)
                step();

            stop();
        }
        """,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        void f()
        {
            run();

            for (int i = 0; i < n; ++i) {
                step();
            }

            stop();
        }
        """
    )
    assert run(root, str(path)).returncode == 0


def test_fix_consistency_add(root: pathlib.Path) -> None:
    path = write(
        root,
        "fix-consistency.cpp",
        """
        void f()
        {
            run();

            if (a) {
                first();
                second();
            } else if (b)
                third();
            else
                fourth();

            stop();
        }
        """,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        void f()
        {
            run();

            if (a) {
                first();
                second();
            } else if (b) {
                third();
            }
            else {
                fourth();
            }

            stop();
        }
        """
    )
    assert run(root, str(path)).returncode == 0


def test_doc_comment_attaches_to_function(root: pathlib.Path) -> None:
    expect(
        root,
        "doc-comment.cpp",
        """
        int f()
        {
            return 1;
        }

        /* docs */
        int g()
        {
            return 2;
        }
        """,
    )


def test_doc_comment_needs_blank_above(root: pathlib.Path) -> None:
    path = expect(
        root,
        "doc-comment-blank.cpp",
        """
        int f()
        {
            return 1;
        }

        int x = 0;
        /* docs */
        int g()
        {
            return 2;
        }
        """,
        before=1,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        int f()
        {
            return 1;
        }

        int x = 0;

        /* docs */
        int g()
        {
            return 2;
        }
        """
    )


def test_loop_inside_bare_block(root: pathlib.Path) -> None:
    expect(
        root,
        "bare-loop.cpp",
        """
        void f()
        {
            run();

            {
                for (auto& item : entries)
                    if (item.used) {
                        work();
                    }
            }

            stop();
        }
        """,
        loop_add=1,
    )


def test_nested_block_boundaries(root: pathlib.Path) -> None:
    expect(
        root,
        "nested.cpp",
        """
        void f()
        {
            if (a) {
                for (int i = 0; i < n; ++i) {
                    work();
                }
            }
        }
        """,
    )


def test_directives(root: pathlib.Path) -> None:
    expect(
        root,
        "directives.cpp",
        """
        void f()
        {
            run();
        #if FEATURE
            if (a)
                work();
        #endif
            stop();
        }
        """,
    )


def test_remove_single_statement_braces(root: pathlib.Path) -> None:
    expect(
        root,
        "remove.cpp",
        """
        void f()
        {
            run();

            if (a) {
                work();
            }

            stop();
        }
        """,
        remove=1,
    )


def test_add_braces_for_consistency(root: pathlib.Path) -> None:
    expect(
        root,
        "add.cpp",
        """
        void f()
        {
            run();

            if (a) {
                first();
                second();
            } else if (b)
                third();
            else
                fourth();

            stop();
        }
        """,
        add=2,
    )


def test_remove_chain_braces(root: pathlib.Path) -> None:
    expect(
        root,
        "remove-chain.cpp",
        """
        void f()
        {
            run();

            if (a) {
                first();
            } else if (b) {
                second();
            } else {
                third();
            }

            stop();
        }
        """,
        remove=3,
    )


def test_macro_body_keeps_braces(root: pathlib.Path) -> None:
    expect(
        root,
        "macro.cpp",
        """
        void f()
        {
            run();

            if (a) {
                ASTIA_CHECK(b);
            }

            stop();
        }
        """,
    )


def test_declaration_body_keeps_braces(root: pathlib.Path) -> None:
    expect(
        root,
        "declaration.cpp",
        """
        void f()
        {
            run();

            if (a) {
                u64 value = 0;
            }

            stop();
        }
        """,
    )


def test_comment_keeps_braces(root: pathlib.Path) -> None:
    expect(
        root,
        "comment.cpp",
        """
        void f()
        {
            run();

            if (a) {
                // explained
                work();
            }

            stop();
        }
        """,
    )


def test_nested_statement_keeps_braces(root: pathlib.Path) -> None:
    expect(
        root,
        "dangling.cpp",
        """
        void f()
        {
            run();

            if (a) {
                if (b)
                    work();
            } else {
                other();
            }

            stop();
        }
        """,
    )


def test_return_needs_blank_line_before(root: pathlib.Path) -> None:
    expect(
        root,
        "return-after.cpp",
        """
        int f()
        {
            run();
            return 1;
        }
        """,
        before=1,
    )


def test_return_first_in_block(root: pathlib.Path) -> None:
    expect(
        root,
        "return-first.cpp",
        """
        int f()
        {
            return 1;
        }
        """,
    )


def test_return_as_body_is_not_checked(root: pathlib.Path) -> None:
    expect(
        root,
        "return-body.cpp",
        """
        int f()
        {
            run();

            if (a)
                return 1;

            return 0;
        }
        """,
    )


def test_strings_and_comments(root: pathlib.Path) -> None:
    expect(
        root,
        "literals.cpp",
        """
        void f()
        {
            const char* text = "if (x) { }";
            // if (y) { }
            /* for (;;) { } */
            run();
            if (a) return;
            stop();
        }
        """,
        before=1,
        after=2,
    )


def test_shared_line_is_skipped(root: pathlib.Path) -> None:
    expect(
        root,
        "shared.cpp",
        """
        void f()
        {
            run(); if (a) return; stop();
        }
        """,
    )


def test_case_label(root: pathlib.Path) -> None:
    expect(
        root,
        "switch.cpp",
        """
        void f()
        {
            switch (a) {
            case 1:
                run();
                if (b) return;
                stop();
                break;
            }
        }
        """,
        before=1,
        after=1,
    )


def test_consecutive_statements(root: pathlib.Path) -> None:
    expect(
        root,
        "consecutive.cpp",
        """
        void f()
        {
            run();
            if (a) return;
            if (b) return;
            stop();
        }
        """,
        before=2,
        after=1,
    )


def test_namespace_boundaries(root: pathlib.Path) -> None:
    expect(
        root,
        "namespace.cpp",
        """
        namespace foo {

            int x;

        }
        """,
        ns_open=1,
        ns_close=1,
    )


def test_namespace_clean(root: pathlib.Path) -> None:
    expect(
        root,
        "namespace-clean.cpp",
        """
        namespace foo {
            int x;

            int y;
        }
        """,
    )


def test_namespace_forms(root: pathlib.Path) -> None:
    expect(
        root,
        "namespace-forms.cpp",
        """
        namespace a {

            namespace b {

                int x;

            }

        }

        namespace {

            int y;

        }

        inline namespace v1 {

            int z;

        }
        """,
        ns_open=4,
        ns_close=4,
    )


def test_namespace_alias_and_using(root: pathlib.Path) -> None:
    expect(
        root,
        "namespace-alias.cpp",
        """
        namespace alias = other;
        using namespace std;

        namespace foo {
            int x;
        }
        """,
    )


def test_fix_namespace(root: pathlib.Path) -> None:
    path = write(
        root,
        "fix-namespace.cpp",
        """
        namespace foo {

            int x;

        }
        """,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        namespace foo {
            int x;
        }
        """
    )
    assert run(root, str(path)).returncode == 0


def test_declaration_group(root: pathlib.Path) -> None:
    expect(
        root,
        "decl-group.cpp",
        """
        void f()
        {
            run();
            int a = 0;
            int b = 0;
            stop();
        }
        """,
        before=1,
        after=1,
    )


def test_declaration_group_at_edges(root: pathlib.Path) -> None:
    expect(
        root,
        "decl-edges.cpp",
        """
        void f()
        {
            int a = 0;
            int b = 0;
        }
        """,
    )


def test_declaration_group_between_statements(root: pathlib.Path) -> None:
    expect(
        root,
        "decl-between.cpp",
        """
        void f()
        {
            module_load_options options{};
            module_name_from_path(kernel_path, options.name);
            const auto opened = co_await open_path(*process, kernel_path, options);
        }
        """,
        before=1,
        after=1,
    )


def test_expressions_are_not_grouped(root: pathlib.Path) -> None:
    expect(
        root,
        "expressions.cpp",
        """
        void f()
        {
            foo();
            bar();
            baz();
        }
        """,
    )


def test_guard_gets_blank_before_assignments(root: pathlib.Path) -> None:
    expect(
        root,
        "guard-group.cpp",
        """
        void f()
        {
            run();

            arch::interrupt_spinlock_guard guard(_lock);
            node->region = copy;
            node->parent_page = index;

            stop();
        }
        """,
        before=1,
    )


def test_assignment_group(root: pathlib.Path) -> None:
    expect(
        root,
        "assign-group.cpp",
        """
        void f()
        {
            run();
            a = 1;
            b = 2;
            stop();
        }
        """,
        before=1,
        after=1,
    )


def test_assignment_group_at_edges(root: pathlib.Path) -> None:
    expect(
        root,
        "assign-edges.cpp",
        """
        void f()
        {
            a = 1;
            b = 2;
        }
        """,
    )


def test_comparisons_are_not_assignments(root: pathlib.Path) -> None:
    expect(
        root,
        "comparisons.cpp",
        """
        void f()
        {
            foo();
            a == b;
            c != d;
            e <= f;
            g >= h;
            bar();
        }
        """,
    )


def test_compound_assignments_are_grouped(root: pathlib.Path) -> None:
    expect(
        root,
        "compound.cpp",
        """
        void f()
        {
            run();
            a += 1;
            b <<= 2;
            c >>= 3;
            stop();
        }
        """,
        before=1,
        after=1,
    )


def test_declaration_kinds_split(root: pathlib.Path) -> None:
    path = write(
        root,
        "decl-kinds.cpp",
        """
        void f()
        {
            run();
            auto* storage = ::new (static_cast<void*>(memory.value)) resolve_scratch{};
            resolve_scratch_owner owner(storage);
            char(&pending)[path_max * 2]  = storage->pending;
            char(&result)[path_max]       = storage->result;
            u32 plen = 0, rlen = 1, links = 0;
            u32 path_length = 0;
            stop();
        }
        """,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        void f()
        {
            run();

            auto* storage = ::new (static_cast<void*>(memory.value)) resolve_scratch{};
            resolve_scratch_owner owner(storage);

            char(&pending)[path_max * 2]  = storage->pending;
            char(&result)[path_max]       = storage->result;

            u32 plen = 0, rlen = 1, links = 0;
            u32 path_length = 0;

            stop();
        }
        """
    )
    assert run(root, str(path)).returncode == 0


def test_declaration_and_assignment_runs_split(root: pathlib.Path) -> None:
    expect(
        root,
        "decl-assign.cpp",
        """
        void f()
        {
            int a = 0;
            a = 1;
        }
        """,
        before=1,
    )


def test_fix_assignments(root: pathlib.Path) -> None:
    path = write(
        root,
        "fix-assignments.cpp",
        """
        void f()
        {
            run();
            a = 1;
            b = 2;
            stop();
        }
        """,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        void f()
        {
            run();

            a = 1;
            b = 2;

            stop();
        }
        """
    )
    assert run(root, str(path)).returncode == 0


def test_class_body_is_not_grouped(root: pathlib.Path) -> None:
    expect(
        root,
        "class-body.cpp",
        """
        class W {
            int a;
            int b;
            void f();
        };
        """,
    )


def test_declaration_and_control_share_boundary(root: pathlib.Path) -> None:
    expect(
        root,
        "decl-control.cpp",
        """
        void f()
        {
            int a = 0;
            if (x) return;
            int b = 0;
        }
        """,
        before=2,
    )


def test_fix_declarations(root: pathlib.Path) -> None:
    path = write(
        root,
        "fix-declarations.cpp",
        """
        void f()
        {
            run();
            int a = 0;
            int b = 0;
            stop();
        }
        """,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        void f()
        {
            run();

            int a = 0;
            int b = 0;

            stop();
        }
        """
    )
    assert run(root, str(path)).returncode == 0


def test_functions_need_blank_lines(root: pathlib.Path) -> None:
    expect(
        root,
        "functions.cpp",
        """
        int f()
        {
            return 1;
        }
        int g()
        {
            return 2;
        }
        """,
        before=1,
    )


def test_functions_at_file_edges(root: pathlib.Path) -> None:
    expect(
        root,
        "function-edges.cpp",
        """
        int f()
        {
            return 1;
        }
        """,
    )


def test_prototypes_may_touch(root: pathlib.Path) -> None:
    expect(
        root,
        "prototypes.cpp",
        """
        void f();
        void g();
        void h();
        """,
    )


def test_function_after_prototypes(root: pathlib.Path) -> None:
    expect(
        root,
        "after-prototypes.cpp",
        """
        void f();
        void g();

        int h()
        {
            return 0;
        }
        """,
    )


def test_class_methods_need_blank_lines(root: pathlib.Path) -> None:
    expect(
        root,
        "methods.cpp",
        """
        class W {
            void f()
            {
            }
            void g()
            {
            }
        };
        """,
        before=1,
    )


def test_method_at_class_edges(root: pathlib.Path) -> None:
    expect(
        root,
        "method-edges.cpp",
        """
        class W {
            void f()
            {
            }
        };
        """,
    )


def test_constructor_init_list_is_a_definition(root: pathlib.Path) -> None:
    expect(
        root,
        "constructor.cpp",
        """
        class W {
            W()
                : value_(0)
            {
                value_ = 1;
            }

            int value_;
        };
        """,
    )


def test_lambda_is_not_a_function_definition(root: pathlib.Path) -> None:
    expect(
        root,
        "lambda.cpp",
        """
        void f()
        {
            auto g = []() {
                return 1;
            };
        }
        """,
    )


def test_type_definitions_need_blank_lines(root: pathlib.Path) -> None:
    expect(
        root,
        "types.cpp",
        """
        struct A {
            int x;
        };
        struct B {
            int y;
        };
        """,
        before=1,
    )


def test_type_definitions_at_edges(root: pathlib.Path) -> None:
    expect(
        root,
        "type-edges.cpp",
        """
        struct A {
            int x;
        };
        """,
    )


def test_forward_declarations_may_touch(root: pathlib.Path) -> None:
    expect(
        root,
        "forward.cpp",
        """
        struct A;
        struct B;
        enum class C;
        """,
    )


def test_typedef_structs_need_blank_lines(root: pathlib.Path) -> None:
    expect(
        root,
        "typedef-types.cpp",
        """
        typedef struct {
            int x;
        } A;
        typedef struct {
            int y;
        } B;
        """,
        before=1,
    )


def test_nested_type_definitions(root: pathlib.Path) -> None:
    expect(
        root,
        "nested-types.cpp",
        """
        class W {
            struct Inner {
                int x;
            };
            struct Other {
                int y;
            };
        };
        """,
        before=1,
    )


def test_enum_definitions_need_blank_lines(root: pathlib.Path) -> None:
    expect(
        root,
        "enums.cpp",
        """
        enum class A {
            one,
        };
        enum class B {
            two,
        };
        """,
        before=1,
    )


def test_fix_type_definitions(root: pathlib.Path) -> None:
    path = write(
        root,
        "fix-types.cpp",
        """
        struct A {
            int x;
        };
        struct B {
            int y;
        };
        """,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        struct A {
            int x;
        };

        struct B {
            int y;
        };
        """
    )
    assert run(root, str(path)).returncode == 0


def test_redundant_semicolon_after_block(root: pathlib.Path) -> None:
    path = expect(
        root,
        "semicolon-if.cpp",
        """
        void f()
        {
            if (x) {
                first();
                second();
            };
        }
        """,
        semicolons=1,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        void f()
        {
            if (x) {
                first();
                second();
            }
        }
        """
    )
    assert run(root, str(path)).returncode == 0


def test_redundant_semicolon_after_function(root: pathlib.Path) -> None:
    expect(
        root,
        "semicolon-function.cpp",
        """
        int f()
        {
            return 0;
        };
        """,
        semicolons=1,
    )


def test_redundant_semicolon_after_bare_block(root: pathlib.Path) -> None:
    expect(
        root,
        "semicolon-block.cpp",
        """
        void f()
        {
            {
                work();
            };
        }
        """,
        semicolons=1,
    )


def test_type_and_initializer_keep_semicolon(root: pathlib.Path) -> None:
    expect(
        root,
        "semicolon-required.cpp",
        """
        struct X {
            int a;
        };

        enum class E {
            one,
        };

        void f()
        {
            auto g = [] {
                return 1;
            };

            X value{};
        }
        """,
    )


def test_do_while_keeps_semicolon(root: pathlib.Path) -> None:
    expect(
        root,
        "semicolon-do.cpp",
        """
        void f()
        {
            do {
                work();
            } while (x);
        }
        """,
    )


def test_requires_expression_is_not_a_function(root: pathlib.Path) -> None:
    expect(
        root,
        "requires.cpp",
        """
        template <typename T>
        concept C = requires(T value) {
            { value.f() } -> std::same_as<int>;
            { value.g() };
        };
        """,
    )


def test_compound_literal_is_not_a_function(root: pathlib.Path) -> None:
    expect(
        root,
        "compound-literal.c",
        """
        void f()
        {
            *slot = (astia_sqe){0};
        }
        """,
    )


def test_qualified_call_body_loses_braces(root: pathlib.Path) -> None:
    expect(
        root,
        "qualified-if.cpp",
        """
        void f()
        {
            run();

            if (a) {
                mm::kernel_frames().release(x);
            }

            stop();
        }
        """,
        remove=1,
    )


def test_for_ever_rewritten(root: pathlib.Path) -> None:
    path = write(
        root,
        "forever.cpp",
        """
        void f()
        {
            for (;;) {
                step();
            }
        }
        """,
    )
    result = run(root, str(path))
    assert result.returncode == 1, (result.stdout, result.stderr)
    assert result.stdout.count(FOREVER) == 1, result.stdout
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        void f()
        {
            while (true) {
                step();
            }
        }
        """
    )


def test_for_ever_braceless(root: pathlib.Path) -> None:
    path = write(
        root,
        "forever-bare.cpp",
        """
        void f()
        {
            for (;;)
                step();
        }
        """,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        void f()
        {
            while (true) {
                step();
            }
        }
        """
    )


def test_for_ever_left_in_c(root: pathlib.Path) -> None:
    expect(
        root,
        "forever.c",
        """
        void f()
        {
            for (;;) {
                step();
            }
        }
        """,
    )


def test_for_ever_with_comment_is_skipped(root: pathlib.Path) -> None:
    expect(
        root,
        "forever-comment.cpp",
        """
        void f()
        {
            for (/* spin */;;) {
                step();
            }
        }
        """,
    )


def test_case_labels_group_with_body(root: pathlib.Path) -> None:
    path = expect(
        root,
        "case-labels.cpp",
        """
        int f(int value)
        {
            switch (value) {
            case 1:
            case 2:

                return 1;

            case 3:
                return 2;

            default:

                return 3;
            }
        }
        """,
        case_blank=4,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        int f(int value)
        {
            switch (value) {
            case 1:
            case 2:
                return 1;
            case 3:
                return 2;
            default:
                return 3;
            }
        }
        """
    )
    assert run(root, str(path)).returncode == 0


def test_case_blank_after_switch_brace(root: pathlib.Path) -> None:
    expect(
        root,
        "case-open.cpp",
        """
        int f(int value)
        {
            switch (value) {

            case 1:
                return 1;
            }
        }
        """,
        case_blank=1,
    )


def test_label_block_with_trailing_comment(root: pathlib.Path) -> None:
    expect(
        root,
        "label-block.cpp",
        """
        int f(int whence)
        {
            switch (whence) {
            default: { /* end */
                u64 size;

                run();
            }
            }
        }
        """,
    )


def test_switch_case_is_not_a_declaration(root: pathlib.Path) -> None:
    expect(
        root,
        "case-decl.cpp",
        """
        int f(int value)
        {
            switch (value) {
            case 1:
                return 1;
            default:
                return 2;
            }
        }
        """,
    )


def test_bare_block_needs_blank_lines(root: pathlib.Path) -> None:
    path = expect(
        root,
        "bare-block.cpp",
        """
        void f()
        {
            run();
            {
                work();
            }
            stop();
        }
        """,
        before=1,
        after=1,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        void f()
        {
            run();

            {
                work();
            }

            stop();
        }
        """
    )


def test_bare_block_at_edges(root: pathlib.Path) -> None:
    expect(
        root,
        "bare-block-edges.cpp",
        """
        void f()
        {
            {
                work();
            }
        }
        """,
    )


def test_trailing_block_comment_is_not_leading(root: pathlib.Path) -> None:
    path = expect(
        root,
        "trailing-comment.cpp",
        """
        void f()
        {
            run();
            size_t value = 0; /* trailing comment
                                 continues here */
            int x;
            stop();
        }
        """,
        before=1,
        after=1,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        void f()
        {
            run();

            size_t value = 0; /* trailing comment
                                 continues here */
            int x;

            stop();
        }
        """
    )
    assert run(root, str(path)).returncode == 0


def test_comment_stays_with_control_statement(root: pathlib.Path) -> None:
    path = expect(
        root,
        "comment-attach.cpp",
        """
        void f()
        {
            run();
            // explained
            if (x) return;
            stop();
        }
        """,
        before=1,
        after=1,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        void f()
        {
            run();

            // explained
            if (x) return;

            stop();
        }
        """
    )


def test_if_checks_previous_assignment(root: pathlib.Path) -> None:
    expect(
        root,
        "if-check.cpp",
        """
        int f()
        {
            auto out = validate();
            if (!out)
                return -1;

            return 0;
        }
        """,
    )


def test_if_checks_previous_assignment_plain(root: pathlib.Path) -> None:
    expect(
        root,
        "if-check-plain.cpp",
        """
        int f()
        {
            auto out = validate();
            if (out)
                return -1;

            return 0;
        }
        """,
    )


def test_if_checks_previous_assignment_with_blank(root: pathlib.Path) -> None:
    path = expect(
        root,
        "if-check-blank.cpp",
        """
        int f()
        {
            auto out = validate();

            if (!out)
                return -1;

            return 0;
        }
        """,
        checks=1,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        int f()
        {
            auto out = validate();
            if (!out)
                return -1;

            return 0;
        }
        """
    )


def test_if_checks_other_variable(root: pathlib.Path) -> None:
    expect(
        root,
        "if-check-other.cpp",
        """
        int f()
        {
            auto out = validate();
            if (other)
                return -1;

            return 0;
        }
        """,
        before=1,
    )


def test_if_checks_complex_condition(root: pathlib.Path) -> None:
    expect(
        root,
        "if-check-complex.cpp",
        """
        int f()
        {
            auto out = validate();
            if (!out && flag)
                return -1;

            return 0;
        }
        """,
        before=1,
    )


def test_if_compares_assigned_pointer(root: pathlib.Path) -> None:
    expect(
        root,
        "if-check-null.cpp",
        """
        int f()
        {
            auto* process = current_process();
            if (process == nullptr)
                return -1;

            return 0;
        }
        """,
    )


def test_if_compares_assigned_pointer_not_equal(root: pathlib.Path) -> None:
    expect(
        root,
        "if-check-null-ne.cpp",
        """
        int f()
        {
            auto* process = current_process();
            if (process != nullptr)
                return -1;

            return 0;
        }
        """,
    )


def test_check_after_multi_declaration_keeps_blank(root: pathlib.Path) -> None:
    expect(
        root,
        "if-check-run.cpp",
        """
        int f()
        {
            auto first = one();
            auto out = two();

            if (!out)
                return -1;

            return 0;
        }
        """,
    )


def test_if_compacts_named_bound(root: pathlib.Path) -> None:
    path = expect(
        root,
        "if-check-bound.cpp",
        """
        int f()
        {
            u32 clen = end - start;

            if (clen >= path_max)
                return -1;

            return 0;
        }
        """,
        checks=1,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        int f()
        {
            u32 clen = end - start;
            if (clen >= path_max)
                return -1;

            return 0;
        }
        """
    )


def test_if_compacts_multiline_assignment(root: pathlib.Path) -> None:
    path = expect(
        root,
        "if-check-multiline.cpp",
        """
        int f()
        {
            long rc = resolve(path,
                              buffer, size);

            if (rc < 0)
                return rc;

            return 0;
        }
        """,
        checks=1,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        int f()
        {
            long rc = resolve(path,
                              buffer, size);
            if (rc < 0)
                return rc;

            return 0;
        }
        """
    )


def test_if_compacts_pointer_null_check(root: pathlib.Path) -> None:
    path = expect(
        root,
        "if-check-pointer.cpp",
        """
        int f()
        {
            auto* process = current_process();

            if (process == nullptr)
                return -1;

            return 0;
        }
        """,
        checks=1,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        int f()
        {
            auto* process = current_process();
            if (process == nullptr)
                return -1;

            return 0;
        }
        """
    )


def test_if_compares_to_another_variable(root: pathlib.Path) -> None:
    expect(
        root,
        "if-check-vars.cpp",
        """
        int f()
        {
            auto* process = current_process();
            if (process == other)
                return -1;

            return 0;
        }
        """,
    )


def test_fix_blank_lines(root: pathlib.Path) -> None:
    path = write(
        root,
        "fix-lines.cpp",
        """
        void f()
        {
            run();
            if (a) return;
            stop();
        }
        """,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        void f()
        {
            run();

            if (a) return;

            stop();
        }
        """
    )
    assert run(root, str(path)).returncode == 0


def test_fix_braces(root: pathlib.Path) -> None:
    path = write(
        root,
        "fix-braces.cpp",
        """
        void f()
        {
            run();

            if (a) {
                work();
            }

            stop();
        }
        """,
    )
    result = run(root, "--fix", str(path))
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert path.read_text(encoding="utf-8") == textwrap.dedent(
        """\
        void f()
        {
            run();

            if (a) work();

            stop();
        }
        """
    )
    assert run(root, str(path)).returncode == 0


def test_stale_base_uses_merge_base(root: pathlib.Path) -> None:
    repo = root / "merge-base"
    repo.mkdir()
    git(repo, "init", "-q")
    git(repo, "config", "user.email", "test@example.com")
    git(repo, "config", "user.name", "test")
    write(repo, "a.cpp", "int a()\n{\n    return 0;\n}\n")
    write(
        repo,
        "b.cpp",
        """
        void b()
        {
            run();
            if (x) return;
            stop();
        }
        """,
    )
    git(repo, "add", ".")
    git(repo, "commit", "-q", "-m", "base")
    base = git(repo, "rev-parse", "HEAD")
    write(
        repo,
        "b.cpp",
        "void b()\n{\n    run();\n\n    if (x) return;\n\n    stop();\n}\n",
    )
    git(repo, "commit", "-q", "-am", "main")
    main = git(repo, "rev-parse", "HEAD")
    git(repo, "checkout", "-q", "-b", "feature", base)
    write(repo, "a.cpp", "int a()\n{\n    return 1;\n}\n")
    git(repo, "commit", "-q", "-am", "feature")
    head = git(repo, "rev-parse", "HEAD")
    result = run(repo, "--base", main, "--head", head)
    assert result.returncode == 0, (result.stdout, result.stderr)


def test_stale_base_prefers_origin_main(root: pathlib.Path) -> None:
    repo = root / "origin-main"
    repo.mkdir()
    git(repo, "init", "-q")
    git(repo, "config", "user.email", "test@example.com")
    git(repo, "config", "user.name", "test")
    write(repo, "a.cpp", "int a()\n{\n    return 0;\n}\n")
    write(repo, "b.cpp", "int b()\n{\n    return 0;\n}\n")
    git(repo, "add", ".")
    git(repo, "commit", "-q", "-m", "old-base")
    old_base = git(repo, "rev-parse", "HEAD")
    write(
        repo,
        "b.cpp",
        """
        void b()
        {
            run();
            if (x) return;
            stop();
        }
        """,
    )
    git(repo, "commit", "-q", "-am", "main")
    main = git(repo, "rev-parse", "HEAD")
    git(repo, "update-ref", "refs/remotes/origin/main", main)
    git(repo, "checkout", "-q", "-b", "feature")
    write(repo, "a.cpp", "int a()\n{\n    return 1;\n}\n")
    git(repo, "commit", "-q", "-am", "feature")
    head = git(repo, "rev-parse", "HEAD")
    result = run(repo, "--base", old_base, "--head", head)
    assert result.returncode == 0, (result.stdout, result.stderr)


def test_changed_lines(root: pathlib.Path) -> None:
    repo = root / "repo"
    repo.mkdir()
    git(repo, "init", "-q")
    git(repo, "config", "user.email", "test@example.com")
    git(repo, "config", "user.name", "test")

    path = write(
        repo,
        "scope.cpp",
        """
        void f()
        {
            run();
            if (a) return;
            stop();
        }
        """,
    )
    git(repo, "add", "scope.cpp")
    git(repo, "commit", "-q", "-m", "base")
    base = git(repo, "rev-parse", "HEAD")

    with path.open("a", encoding="utf-8") as handle:
        handle.write("\n// unrelated tail\n")
    git(repo, "commit", "-q", "-am", "tail")
    tail = git(repo, "rev-parse", "HEAD")

    result = run(repo, "--base", base, "--head", tail)
    assert result.returncode == 0, (result.stdout, result.stderr)

    text = path.read_text(encoding="utf-8").replace("return", "return 1")
    path.write_text(text, encoding="utf-8")
    git(repo, "commit", "-q", "-am", "touch statement")
    touched = git(repo, "rev-parse", "HEAD")

    result = run(repo, "--base", tail, "--head", touched)
    assert result.returncode == 1, (result.stdout, result.stderr)


def test_deleted_blank_line(root: pathlib.Path) -> None:
    repo = root / "deleted-blank"
    repo.mkdir()
    git(repo, "init", "-q")
    git(repo, "config", "user.email", "test@example.com")
    git(repo, "config", "user.name", "test")

    path = write(
        repo,
        "deleted.cpp",
        """
        void f()
        {
            run();

            if (a)
                work();

            stop();
        }
        """,
    )
    git(repo, "add", "deleted.cpp")
    git(repo, "commit", "-q", "-m", "base")
    base = git(repo, "rev-parse", "HEAD")

    text = path.read_text(encoding="utf-8")
    assert "work();\n\n    stop();" in text
    path.write_text(text.replace("work();\n\n    stop();", "work();\n    stop();"))
    git(repo, "commit", "-q", "-am", "drop blank line")
    head = git(repo, "rev-parse", "HEAD")

    result = run(repo, "--base", base, "--head", head)
    assert result.returncode == 1, (result.stdout, result.stderr)


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="cpp-style-") as directory:
        root = pathlib.Path(directory)
        test_clean(root)
        test_first_and_last_statement(root)
        test_blank_lines_both_sides(root)
        test_else_chain_blank_lines(root)
        test_loops_keep_braces(root)
        test_for_requires_braces(root)
        test_while_requires_braces(root)
        test_nested_braceless_loop_gets_braces(root)
        test_do_requires_braces(root)
        test_qualified_call_loop_body_gets_braces(root)
        test_fix_for_braces(root)
        test_fix_consistency_add(root)
        test_doc_comment_attaches_to_function(root)
        test_doc_comment_needs_blank_above(root)
        test_loop_inside_bare_block(root)
        test_nested_block_boundaries(root)
        test_directives(root)
        test_remove_single_statement_braces(root)
        test_add_braces_for_consistency(root)
        test_remove_chain_braces(root)
        test_macro_body_keeps_braces(root)
        test_declaration_body_keeps_braces(root)
        test_comment_keeps_braces(root)
        test_nested_statement_keeps_braces(root)
        test_return_needs_blank_line_before(root)
        test_return_first_in_block(root)
        test_return_as_body_is_not_checked(root)
        test_strings_and_comments(root)
        test_shared_line_is_skipped(root)
        test_case_label(root)
        test_consecutive_statements(root)
        test_declaration_group(root)
        test_declaration_group_at_edges(root)
        test_declaration_group_between_statements(root)
        test_expressions_are_not_grouped(root)
        test_assignment_group(root)
        test_assignment_group_at_edges(root)
        test_comparisons_are_not_assignments(root)
        test_compound_assignments_are_grouped(root)
        test_declaration_kinds_split(root)
        test_declaration_and_assignment_runs_split(root)
        test_guard_gets_blank_before_assignments(root)
        test_fix_assignments(root)
        test_class_body_is_not_grouped(root)
        test_declaration_and_control_share_boundary(root)
        test_fix_declarations(root)
        test_functions_need_blank_lines(root)
        test_functions_at_file_edges(root)
        test_prototypes_may_touch(root)
        test_function_after_prototypes(root)
        test_class_methods_need_blank_lines(root)
        test_method_at_class_edges(root)
        test_constructor_init_list_is_a_definition(root)
        test_lambda_is_not_a_function_definition(root)
        test_type_definitions_need_blank_lines(root)
        test_type_definitions_at_edges(root)
        test_forward_declarations_may_touch(root)
        test_typedef_structs_need_blank_lines(root)
        test_nested_type_definitions(root)
        test_enum_definitions_need_blank_lines(root)
        test_fix_type_definitions(root)
        test_redundant_semicolon_after_block(root)
        test_redundant_semicolon_after_function(root)
        test_redundant_semicolon_after_bare_block(root)
        test_type_and_initializer_keep_semicolon(root)
        test_do_while_keeps_semicolon(root)
        test_requires_expression_is_not_a_function(root)
        test_compound_literal_is_not_a_function(root)
        test_qualified_call_body_loses_braces(root)
        test_for_ever_rewritten(root)
        test_for_ever_braceless(root)
        test_for_ever_left_in_c(root)
        test_for_ever_with_comment_is_skipped(root)
        test_case_labels_group_with_body(root)
        test_case_blank_after_switch_brace(root)
        test_label_block_with_trailing_comment(root)
        test_switch_case_is_not_a_declaration(root)
        test_bare_block_needs_blank_lines(root)
        test_bare_block_at_edges(root)
        test_trailing_block_comment_is_not_leading(root)
        test_comment_stays_with_control_statement(root)
        test_if_checks_previous_assignment(root)
        test_if_checks_previous_assignment_plain(root)
        test_if_checks_previous_assignment_with_blank(root)
        test_if_checks_other_variable(root)
        test_if_checks_complex_condition(root)
        test_if_compares_assigned_pointer(root)
        test_if_compares_assigned_pointer_not_equal(root)
        test_check_after_multi_declaration_keeps_blank(root)
        test_if_compacts_named_bound(root)
        test_if_compacts_multiline_assignment(root)
        test_if_compacts_pointer_null_check(root)
        test_if_compares_to_another_variable(root)
        test_namespace_boundaries(root)
        test_namespace_clean(root)
        test_namespace_forms(root)
        test_namespace_alias_and_using(root)
        test_fix_namespace(root)
        test_fix_blank_lines(root)
        test_fix_braces(root)
        test_stale_base_uses_merge_base(root)
        test_stale_base_prefers_origin_main(root)
        test_changed_lines(root)
        test_deleted_blank_line(root)
    print("cpp-style checker test: passed")


if __name__ == "__main__":
    main()
