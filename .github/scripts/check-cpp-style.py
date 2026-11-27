#!/usr/bin/env python3
"""Check the repository C/C++ statement, brace, and declaration layout.

Two lexical rules are enforced.

* A control statement (``if``, ``for``, ``while``, ``switch``, ``do``,
  ``try``) or a bare ``{ ... }`` block that starts a line and ends a line must
  be surrounded by blank lines.  A block-level ``return`` only needs a blank
  line before it.  An ``if`` that only checks (``x``, ``!x``, a comparison of ``x`` with a
  literal such as ``x == nullptr`` or ``x < 0``) the variable assigned on the line directly above is kept
  attached to it; an existing blank line between them is removed.
* A single-statement ``if``/``else`` body must not use braces.  ``for``,
  ``while``, and ``do`` bodies must always use braces, including when the loop
  is itself an unbraced body of another statement.  When one branch of an
  ``if``/``else if``/``else`` chain has to keep braces, every branch in the
  chain is braced: missing braces are added and removable ones are kept.
* A ``namespace`` body has no blank line immediately after its opening brace or
  immediately before its closing brace.
* A maximal run of consecutive declaration or assignment statements is
  separated from the statements around it by a blank line, with no blank lines
  inside the run.
* A function or method definition is separated from its neighbours by a blank
  line; declarations and prototypes need no separation.
* A ``struct``, ``class``, ``enum``, or ``union`` definition is separated from
  its neighbours by a blank line; forward declarations need no separation.
* A statement block is not followed by a redundant ``;``; type definitions keep
  the semicolon that terminates them.
* In C++ sources, ``for (;;)`` is rewritten as ``while (true)``.  C sources are
  left alone because ``true`` is not defined there.
* No blank line is kept on either side of a case/default label.

The rules are lexical, so they do not need a compilation database: comments,
string and character literals, and preprocessor directives are ignored, and
control statements that are only the body of another control statement are not
checked.  Neighbours that already separate a statement are exempt: a block
opener (``{``), a closing brace, a ``case``/``default`` or access label, a
preprocessor directive, and the start or end of the file.  Statements that
share a line with other code are skipped.

Braces are only removed around bodies that are clearly safe to unbrace: calls,
assignments, and ``return``/``break``/``continue``/``goto``/``throw``
statements.  Macro calls, declarations, nested control statements, and bodies
containing comments or directives keep their braces.

Run with ``--fix`` to apply both fixes in place, then run the repository
formatter to re-indent unbraced bodies.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys
from dataclasses import dataclass

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
CXX_SUFFIXES = {".cc", ".cpp", ".cxx", ".hh", ".hpp", ".hxx"}
# Imported sources keep their upstream layout and are not Astia style input.
VENDORED_PREFIXES = ("third_party/",)
CONTROL_KEYWORDS = frozenset({"if", "for", "while", "switch", "do", "try"})
RETURN_KEYWORDS = frozenset({"return", "co_return"})
RAW_STRING_PREFIXES = frozenset({"R", "u8R", "uR", "UR", "LR"})
LABEL_RE = re.compile(r"^(?:case\b.*|default|public|private|protected)\s*:$")
STATEMENT_KEYWORDS = frozenset(
    {
        "return",
        "co_return",
        "break",
        "continue",
        "goto",
        "throw",
        "delete",
        "co_await",
        "co_yield",
        "this",
        "new",
        "sizeof",
    }
)
MULTI_STATEMENT_MACROS = frozenset({"pthread_cleanup_push", "pthread_cleanup_pop"})
CONTROL_PAREN_KEYWORDS = frozenset({"if", "for", "while", "switch", "catch"})
FUNCTION_QUALIFIERS = frozenset(
    {"const", "volatile", "noexcept", "override", "final", "mutable", "&"}
)
LEADING_DECLARATION_SPECIFIERS = frozenset(
    {
        "alignas",
        "class",
        "const",
        "consteval",
        "constexpr",
        "constinit",
        "enum",
        "extern",
        "friend",
        "inline",
        "mutable",
        "register",
        "static",
        "struct",
        "thread_local",
        "typename",
        "union",
        "volatile",
    }
)
DECLARATION_KEYWORDS = frozenset(
    {
        "alignas",
        "auto",
        "bool",
        "char",
        "char8_t",
        "char16_t",
        "char32_t",
        "class",
        "const",
        "consteval",
        "constexpr",
        "constinit",
        "decltype",
        "double",
        "enum",
        "explicit",
        "extern",
        "float",
        "friend",
        "inline",
        "int",
        "long",
        "mutable",
        "namespace",
        "register",
        "short",
        "signed",
        "static",
        "static_assert",
        "struct",
        "template",
        "thread_local",
        "typedef",
        "typename",
        "union",
        "unsigned",
        "using",
        "virtual",
        "void",
        "volatile",
        "wchar_t",
    }
)
EXPRESSION_STARTS = frozenset({"(", "!", "~", "*", "&", "-", "+"})
STATEMENT_BLOCK_PRECEDERS = frozenset(
    {
        ")",
        "]",
        "}",
        ";",
        "{",
        ":",
        "try",
        "else",
        "do",
        "const",
        "volatile",
        "noexcept",
        "mutable",
        "&",
    }
)
EXPRESSION_OPERATORS = frozenset(
    {
        "(",
        ".",
        "::",
        "[",
        "=",
        "?",
        "->",
        "++",
        "--",
        "+=",
        "-=",
        "*=",
        "/=",
        "%=",
        "&=",
        "|=",
        "^=",
        "<<=",
        ">>=",
    }
)


@dataclass(frozen=True)
class Token:
    kind: str
    text: str
    line: int
    col: int
    end_line: int
    end_col: int
    offset: int
    end_offset: int


@dataclass(frozen=True)
class Statement:
    keyword: str
    line: int
    col: int
    end_line: int
    end_col: int
    start_index: int


@dataclass(frozen=True)
class Finding:
    kind: str
    line: int
    col: int
    keyword: str = ""
    start_line: int = 0
    end_line: int = 0
    action: str | None = None
    open_index: int | None = None
    close_index: int | None = None
    body_start: int | None = None
    body_end: int | None = None
    token_index: int | None = None


def _scan_quoted(source: str, start: int, quote: str) -> int:
    index = start + 1
    length = len(source)
    while index < length:
        char = source[index]
        if char == "\\":
            index += 2
            continue
        if char == quote:
            return index + 1
        index += 1
    return length


def _scan_number(source: str, start: int) -> int:
    index = start + 1
    length = len(source)
    while index < length:
        char = source[index]
        if char.isalnum() or char in "_'.":
            index += 1
        elif char in "+-" and source[index - 1] in "eEpP":
            index += 1
        else:
            break
    return index


def _scan_raw_string(source: str, quote: int) -> int:
    length = len(source)
    index = quote + 1
    limit = min(length, index + 16)
    while index < limit and source[index] not in "()\\ \t\n":
        index += 1
    if index >= length or source[index] != "(":
        return quote + 1
    delimiter = source[quote + 1 : index]
    closing = ")" + delimiter + '"'
    end = source.find(closing, index + 1)
    return length if end < 0 else end + len(closing)


def lex(
    source: str,
) -> tuple[
    list[Token],
    set[int],
    list[tuple[int, int]],
    list[tuple[int, int]],
    set[int],
]:
    tokens: list[Token] = []
    directives: set[int] = set()
    directive_ranges: list[tuple[int, int]] = []
    comment_ranges: list[tuple[int, int]] = []
    comment_lines: set[int] = set()
    length = len(source)
    index = 0
    line = 1
    col = 1
    at_line_start = True

    def bump(segment: str) -> None:
        nonlocal index, line, col
        newlines = segment.count("\n")
        if newlines:
            line += newlines
            col = len(segment) - segment.rfind("\n")
        else:
            col += len(segment)
        index += len(segment)

    while index < length:
        char = source[index]
        if char == "\n":
            bump(char)
            at_line_start = True
            continue
        if char in " \t\r\f\v":
            bump(char)
            continue

        start_offset = index
        start_line, start_col = line, col

        if char == "#" and at_line_start:
            end = index
            while end < length:
                if source[end] == "\\" and end + 1 < length and source[end + 1] == "\n":
                    end += 2
                    continue
                if source[end] == "\n":
                    break
                end += 1
            segment = source[index:end]
            directive_ranges.append((start_offset, end))
            for offset in range(segment.count("\n") + 1):
                directives.add(start_line + offset)
            bump(segment)
            at_line_start = False
            continue

        if source.startswith("//", index):
            end = source.find("\n", index)
            if end < 0:
                end = length
            comment_ranges.append((start_offset, end))
            line_start = source.rfind("\n", 0, index) + 1
            if not source[line_start:index].strip():
                comment_lines.add(start_line)
            bump(source[index:end])
            at_line_start = False
            continue

        if source.startswith("/*", index):
            end = source.find("*/", index + 2)
            end = length if end < 0 else end + 2
            comment_ranges.append((start_offset, end))
            segment = source[index:end]
            line_start = source.rfind("\n", 0, index) + 1
            starts_alone = not source[line_start:index].strip()
            newlines = segment.count("\n")
            line_end = source.find("\n", end)
            if line_end < 0:
                line_end = length
            if newlines == 0:
                if starts_alone and not source[end:line_end].strip():
                    comment_lines.add(start_line)
            else:
                if starts_alone:
                    comment_lines.add(start_line)
                for offset in range(1, newlines):
                    comment_lines.add(start_line + offset)
                if not source[end:line_end].strip():
                    comment_lines.add(start_line + newlines)
            bump(segment)
            at_line_start = False
            continue

        if char in "\"'":
            end = _scan_quoted(source, index, char)
            segment = source[index:end]
            bump(segment)
            tokens.append(
                Token(
                    "string",
                    segment,
                    start_line,
                    start_col,
                    line,
                    col,
                    start_offset,
                    index,
                )
            )
            at_line_start = False
            continue

        if char.isdigit() or (
            char == "." and index + 1 < length and source[index + 1].isdigit()
        ):
            end = _scan_number(source, index)
            segment = source[index:end]
            bump(segment)
            tokens.append(
                Token(
                    "number",
                    segment,
                    start_line,
                    start_col,
                    line,
                    col,
                    start_offset,
                    index,
                )
            )
            at_line_start = False
            continue

        if char.isalpha() or char in "_$" or ord(char) > 127:
            end = index + 1
            while end < length and (
                source[end].isalnum() or source[end] in "_$" or ord(source[end]) > 127
            ):
                end += 1
            word = source[index:end]
            if word in RAW_STRING_PREFIXES and end < length and source[end] == '"':
                end = _scan_raw_string(source, end)
                segment = source[index:end]
                bump(segment)
                tokens.append(
                    Token(
                        "string",
                        segment,
                        start_line,
                        start_col,
                        line,
                        col,
                        start_offset,
                        index,
                    )
                )
            else:
                bump(word)
                tokens.append(
                    Token(
                        "word",
                        word,
                        start_line,
                        start_col,
                        line,
                        col,
                        start_offset,
                        index,
                    )
                )
            at_line_start = False
            continue

        bump(char)
        tokens.append(
            Token(
                "punct",
                char,
                start_line,
                start_col,
                line,
                col,
                start_offset,
                index,
            )
        )
        at_line_start = False

    return tokens, directives, directive_ranges, comment_ranges, comment_lines


def build_matches(tokens: list[Token]) -> dict[int, int]:
    matches: dict[int, int] = {}
    stack: list[int] = []
    opener_for = {")": "(", "]": "[", "}": "{"}
    for index, token in enumerate(tokens):
        if token.text in "([{":
            stack.append(index)
        elif token.text in ")]}":
            if stack and tokens[stack[-1]].text == opener_for[token.text]:
                opener = stack.pop()
                matches[opener] = index
                matches[index] = opener
    return matches


def statement_end(
    tokens: list[Token],
    index: int,
    limit: int,
    matches: dict[int, int],
) -> int | None:
    if index >= limit:
        return None
    text = tokens[index].text

    if text == "{":
        close = matches.get(index)
        return close if close is not None and close < limit else None

    if text == ";":
        return index

    if text in ("if", "for", "while", "switch"):
        cursor = index + 1
        if cursor < limit and tokens[cursor].text == "constexpr":
            cursor += 1
        if cursor >= limit or tokens[cursor].text != "(":
            return None
        close = matches.get(cursor)
        if close is None or close >= limit:
            return None
        body = statement_end(tokens, close + 1, limit, matches)
        if body is None:
            return None
        if text == "if":
            following = body + 1
            if following < limit and tokens[following].text == "else":
                body = statement_end(tokens, following + 1, limit, matches)
        return body

    if text == "do":
        body = statement_end(tokens, index + 1, limit, matches)
        if body is None:
            return None
        cursor = body + 1
        if cursor >= limit or tokens[cursor].text != "while":
            return None
        cursor += 1
        if cursor >= limit or tokens[cursor].text != "(":
            return None
        close = matches.get(cursor)
        if close is None:
            return None
        cursor = close + 1
        if cursor < limit and tokens[cursor].text == ";":
            return cursor
        return None

    if text == "try":
        cursor = index + 1
        if cursor >= limit or tokens[cursor].text != "{":
            return None
        close = matches.get(cursor)
        if close is None:
            return None
        cursor = close + 1
        while cursor < limit and tokens[cursor].text == "catch":
            cursor += 1
            if cursor >= limit or tokens[cursor].text != "(":
                return None
            close = matches.get(cursor)
            if close is None:
                return None
            cursor = close + 1
            if cursor >= limit or tokens[cursor].text != "{":
                return None
            close = matches.get(cursor)
            if close is None:
                return None
            cursor = close + 1
        return cursor - 1

    if text == "case":
        cursor = index + 1
        while cursor < limit:
            current = tokens[cursor].text
            if current in "([{":
                close = matches.get(cursor)
                if close is None:
                    return None
                cursor = close + 1
                continue
            if current == ":":
                return cursor
            if current in ";}":
                return None
            cursor += 1
        return None

    if text == "default" and index + 1 < limit and tokens[index + 1].text == ":":
        return index + 1

    if (
        tokens[index].kind == "word"
        and index + 1 < limit
        and tokens[index + 1].text == ":"
        and not (index + 2 < limit and tokens[index + 2].text == ":")
    ):
        return index + 1

    cursor = index
    while cursor < limit:
        current = tokens[cursor].text
        if current == ";":
            return cursor
        if current in "([{":
            close = matches.get(cursor)
            if close is None:
                return None
            cursor = close + 1
            continue
        if current == "}":
            return None
        cursor += 1
    return None


def block_statement_lists(
    tokens: list[Token], matches: dict[int, int]
) -> list[list[tuple[int, int]]]:
    blocks: list[list[tuple[int, int]]] = []
    for open_index, close_index in matches.items():
        if tokens[open_index].text != "{":
            continue
        if (
            open_index == 0
            or tokens[open_index - 1].text not in STATEMENT_BLOCK_PRECEDERS
        ):
            continue
        statements: list[tuple[int, int]] = []
        index = open_index + 1
        limit = close_index
        while index < limit:
            end = statement_end(tokens, index, limit, matches)
            if end is None:
                index += 1
                continue
            statements.append((index, end))
            index = end + 1
        blocks.append(statements)
    return blocks


def collect_statements(
    tokens: list[Token],
    matches: dict[int, int],
    blocks: list[list[tuple[int, int]]],
) -> list[Statement]:
    statements: list[Statement] = []
    for block in blocks:
        for index, end in block:
            first = tokens[index]
            if (
                first.text in CONTROL_KEYWORDS
                or first.text in RETURN_KEYWORDS
                or first.text == "{"
            ):
                last = tokens[end]
                statements.append(
                    Statement(
                        first.text if first.text != "{" else "block",
                        first.line,
                        first.col,
                        last.end_line,
                        last.end_col,
                        index,
                    )
                )
    statements.sort(key=lambda statement: (statement.line, statement.col))
    return statements


def _continuation(body: list[Token], index: int) -> bool:
    if index >= len(body):
        return False
    text = body[index].text
    if text in {"(", ".", "[", "=", "?", "<"}:
        return True
    if index + 1 < len(body):
        pair = text + body[index + 1].text
        if pair in EXPRESSION_OPERATORS:
            return True
        if (
            pair in {"<<", ">>"}
            and index + 2 < len(body)
            and body[index + 2].text == "="
        ):
            return True
    return False


def _safe_unbraced_body(body: list[Token]) -> bool:
    if not body:
        return False
    first = body[0]
    if first.kind == "punct":
        return first.text in EXPRESSION_STARTS
    if first.kind != "word":
        return False
    if _is_declaration(body):
        return False
    if first.text in DECLARATION_KEYWORDS or first.text in CONTROL_KEYWORDS:
        return False
    if first.text in MULTI_STATEMENT_MACROS:
        return False
    if first.text in STATEMENT_KEYWORDS:
        return True
    if not any(char.islower() for char in first.text):
        return False
    return _continuation(body, 1)


def _skip_attributes(body: list[Token], index: int) -> int:
    while (
        index + 1 < len(body)
        and body[index].text == "["
        and body[index + 1].text == "["
    ):
        depth = 0
        while index < len(body):
            if body[index].text == "[":
                depth += 1
            elif body[index].text == "]":
                depth -= 1
                if depth == 0:
                    index += 1
                    break
            index += 1
    return index


def _skip_balanced(
    body: list[Token], index: int, opener: str, closer: str
) -> int | None:
    depth = 0
    while index < len(body):
        text = body[index].text
        if text == opener:
            depth += 1
        elif text == closer:
            depth -= 1
            if depth == 0:
                return index + 1
        elif text == ";":
            return None
        index += 1
    return None


def _is_declaration(body: list[Token]) -> bool:
    if not body:
        return False
    if body[0].text in {"case", "default"}:
        return False
    index = _skip_attributes(body, 0)
    while index < len(body):
        text = body[index].text
        if text not in LEADING_DECLARATION_SPECIFIERS:
            break
        index += 1
        if text == "alignas" and index < len(body) and body[index].text == "(":
            end = _skip_balanced(body, index, "(", ")")
            if end is None:
                return False
            index = end
    if index >= len(body):
        return False
    text = body[index].text
    if text in STATEMENT_KEYWORDS or text in CONTROL_KEYWORDS:
        return False
    if text in DECLARATION_KEYWORDS:
        return True
    if body[index].kind != "word":
        return False
    index += 1
    while (
        index + 1 < len(body)
        and body[index].text == ":"
        and body[index + 1].text == ":"
    ):
        index += 2
        if index >= len(body) or body[index].kind != "word":
            return False
        index += 1
    if index < len(body) and body[index].text == "<":
        end = _skip_balanced(body, index, "<", ">")
        if end is None:
            return False
        index = end
    while index < len(body) and body[index].text in {"const", "volatile"}:
        index += 1
    while index < len(body) and body[index].text in {"*", "&"}:
        index += 1
    if index >= len(body) or body[index].kind != "word":
        return False
    if index + 1 >= len(body):
        return False
    return body[index + 1].text in {";", "=", "{", "[", ",", "(", ":"}


def _brace_removable(
    tokens: list[Token],
    matches: dict[int, int],
    directive_ranges: list[tuple[int, int]],
    comment_ranges: list[tuple[int, int]],
    open_index: int,
    close_index: int,
) -> bool:
    if matches.get(open_index) != close_index:
        return False
    open_token = tokens[open_index]
    close_token = tokens[close_index]
    start, end = open_token.end_offset, close_token.offset
    for range_start, range_end in directive_ranges + comment_ranges:
        if range_start < end and range_end > start:
            return False
    if close_index <= open_index + 1:
        return False
    body_end = statement_end(tokens, open_index + 1, close_index, matches)
    if body_end is None or body_end != close_index - 1:
        return False
    return _safe_unbraced_body(tokens[open_index + 1 : body_end + 1])


def _if_chain_branches(
    tokens: list[Token], matches: dict[int, int], if_index: int
) -> list[tuple[int, int]] | None:
    branches: list[tuple[int, int]] = []
    index = if_index
    while True:
        cursor = index + 1
        if cursor < len(tokens) and tokens[cursor].text == "constexpr":
            cursor += 1
        if cursor >= len(tokens) or tokens[cursor].text != "(":
            return None
        close = matches.get(cursor)
        if close is None:
            return None
        body_start = close + 1
        body_end = statement_end(tokens, body_start, len(tokens), matches)
        if body_end is None:
            return None
        branches.append((body_start, body_end))
        after = body_end + 1
        if after >= len(tokens) or tokens[after].text != "else":
            return branches
        following = after + 1
        if following < len(tokens) and tokens[following].text == "if":
            index = following
            continue
        else_end = statement_end(tokens, following, len(tokens), matches)
        if else_end is None:
            return None
        branches.append((following, else_end))
        return branches


def brace_findings(
    tokens: list[Token],
    matches: dict[int, int],
    directive_ranges: list[tuple[int, int]],
    comment_ranges: list[tuple[int, int]],
    blocks: list[list[tuple[int, int]]],
) -> list[Finding]:
    findings: list[Finding] = []
    for statement in collect_statements(tokens, matches, blocks):
        if statement.keyword != "if":
            continue
        branches = _if_chain_branches(tokens, matches, statement.start_index)
        if not branches:
            continue
        status: list[tuple[int, int, bool, bool]] = []
        for start, end in branches:
            braced = tokens[start].text == "{"
            removable = braced and _brace_removable(
                tokens, matches, directive_ranges, comment_ranges, start, end
            )
            status.append((start, end, braced, removable))
        required = any(braced and not removable for _, _, braced, removable in status)
        for start, end, braced, removable in status:
            if required:
                if braced:
                    continue
                findings.append(
                    Finding(
                        "braces",
                        tokens[start].line,
                        tokens[start].col,
                        "if",
                        start_line=tokens[start].line,
                        end_line=tokens[end].line,
                        action="add",
                        body_start=start,
                        body_end=end,
                    )
                )
            elif braced and removable:
                findings.append(
                    Finding(
                        "braces",
                        tokens[start].line,
                        tokens[start].col,
                        "if",
                        start_line=tokens[start].line,
                        end_line=tokens[end].line,
                        action="remove",
                        open_index=start,
                        close_index=end,
                        body_start=start + 1,
                        body_end=end - 1,
                    )
                )
    return findings


def loop_brace_findings(
    tokens: list[Token],
    matches: dict[int, int],
    blocks: list[list[tuple[int, int]]],
) -> list[Finding]:
    findings: list[Finding] = []
    for block in blocks:
        for start, _ in block:
            _walk_loop_statement(tokens, matches, start, findings)
    return findings


def _is_label_start(tokens: list[Token], index: int) -> bool:
    return (
        index + 1 < len(tokens)
        and tokens[index].kind == "word"
        and tokens[index + 1].text == ":"
        and not (index + 2 < len(tokens) and tokens[index + 2].text == ":")
    )


def _loop_body_finding(
    tokens: list[Token],
    matches: dict[int, int],
    body: int,
    keyword: str,
    findings: list[Finding],
) -> int | None:
    if body >= len(tokens) or tokens[body].text == "{":
        return None
    if _is_label_start(tokens, body):
        return None
    end = statement_end(tokens, body, len(tokens), matches)
    if end is None:
        return None
    findings.append(
        Finding(
            "braces",
            tokens[body].line,
            tokens[body].col,
            keyword,
            start_line=tokens[body].line,
            end_line=tokens[end].line,
            action="add",
            body_start=body,
            body_end=end,
        )
    )
    return body


def _walk_loop_statement(
    tokens: list[Token],
    matches: dict[int, int],
    index: int,
    findings: list[Finding],
) -> None:
    if index >= len(tokens):
        return
    text = tokens[index].text
    if text in ("for", "while"):
        cursor = index + 1
        if cursor >= len(tokens) or tokens[cursor].text != "(":
            return
        close = matches.get(cursor)
        if close is None:
            return
        body = _loop_body_finding(tokens, matches, close + 1, text, findings)
        if body is not None:
            _walk_loop_statement(tokens, matches, body, findings)
        return
    if text == "if":
        cursor = index + 1
        if cursor < len(tokens) and tokens[cursor].text == "constexpr":
            cursor += 1
        if cursor >= len(tokens) or tokens[cursor].text != "(":
            return
        close = matches.get(cursor)
        if close is None:
            return
        body = close + 1
        _walk_loop_statement(tokens, matches, body, findings)
        end = statement_end(tokens, body, len(tokens), matches)
        if end is None:
            return
        if end + 1 < len(tokens) and tokens[end + 1].text == "else":
            _walk_loop_statement(tokens, matches, end + 2, findings)
        return
    if text == "do":
        body = _loop_body_finding(tokens, matches, index + 1, "do", findings)
        if body is not None:
            _walk_loop_statement(tokens, matches, body, findings)


def _is_requires_body(
    tokens: list[Token], matches: dict[int, int], open_index: int
) -> bool:
    if open_index == 0:
        return False
    previous = tokens[open_index - 1]
    if previous.text == "requires":
        return True
    if previous.text == ")":
        opener = matches.get(open_index - 1)
        if opener is not None and opener > 0 and tokens[opener - 1].text == "requires":
            return True
    return False


def _in_requires_body(
    tokens: list[Token], matches: dict[int, int], open_index: int
) -> bool:
    depth = 0
    cursor = open_index - 1
    while cursor >= 0:
        text = tokens[cursor].text
        if text == "}":
            depth += 1
        elif text == "{":
            if depth == 0:
                return _is_requires_body(tokens, matches, cursor)
            depth -= 1
        cursor -= 1
    return False


def _is_function_body(
    tokens: list[Token], matches: dict[int, int], open_index: int
) -> bool:
    if open_index == 0:
        return False
    if tokens[open_index - 1].text == "requires":
        return False
    cursor = open_index - 1
    while cursor >= 0 and tokens[cursor].text in FUNCTION_QUALIFIERS:
        cursor -= 1
    if cursor < 0:
        return False
    if tokens[cursor].text != ")":
        probe = cursor
        while probe >= 0:
            text = tokens[probe].text
            if text == "->":
                break
            if text in (";", "{", "}", "="):
                return False
            if text in ")]}":
                opener = matches.get(probe)
                if opener is None:
                    return False
                probe = opener - 1
                continue
            probe -= 1
        else:
            return False
        cursor = probe - 1
        if cursor < 0 or tokens[cursor].text != ")":
            return False
    opener = matches.get(cursor)
    if opener is None:
        return False
    before = opener - 1
    if before < 0:
        return False
    if tokens[before].kind != "word":
        return False
    text = tokens[before].text
    if text in CONTROL_PAREN_KEYWORDS or text == "requires":
        return False
    if text == "constexpr" and before > 0 and tokens[before - 1].text == "if":
        return False
    return True


def _declaration_start(
    tokens: list[Token], matches: dict[int, int], open_index: int
) -> int:
    cursor = open_index - 1
    seen_params = False
    while cursor >= 0:
        text = tokens[cursor].text
        if text == "}":
            if seen_params:
                return cursor + 1
            opener = matches.get(cursor)
            if opener is None:
                return cursor + 1
            cursor = opener - 1
            continue
        if text in ")]":
            if text == ")":
                seen_params = True
            opener = matches.get(cursor)
            if opener is None:
                return cursor + 1
            cursor = opener - 1
            continue
        if text in "([{":
            return cursor + 1
        if text == ";":
            return cursor + 1
        if text == ":":
            if cursor > 0 and tokens[cursor - 1].text == ":":
                cursor -= 2
                continue
            if cursor > 0 and tokens[cursor - 1].text in {
                "public",
                "private",
                "protected",
                "case",
                "default",
            }:
                return cursor + 1
            cursor -= 1
            continue
        cursor -= 1
    return 0


def _leading_comment_start(lines: list[str], comment_lines: set[int], line: int) -> int:
    current = line - 1
    while current >= 1 and current in comment_lines:
        current -= 1
    if current >= 1:
        above = lines[current - 1]
        opener = above.find("/*")
        if opener >= 0 and "*/" not in above[opener:] and above[:opener].strip():
            return line
    return current + 1


def _line_indent(line: str) -> str:
    return line[: len(line) - len(line.lstrip(" \t"))]


def _control_indent_line(
    tokens: list[Token], matches: dict[int, int], body_start: int
) -> int:
    cursor = body_start - 1
    if cursor >= 0 and tokens[cursor].text == ")":
        opener = matches.get(cursor)
        if opener is not None and opener > 0:
            cursor = opener - 1
            if tokens[cursor].text == "constexpr" and cursor > 0:
                cursor -= 1
    if cursor < 0:
        return tokens[body_start].line
    return tokens[cursor].line


def _apply_add_braces(text: str, tokens: list[Token], finding: Finding) -> str:
    matches = build_matches(tokens)
    start = tokens[finding.body_start]
    end = tokens[finding.body_end]
    brace_line = tokens[finding.body_start - 1].line
    indent_line = _control_indent_line(tokens, matches, finding.body_start)
    lines = text.splitlines(keepends=True)
    header_indent = _line_indent(lines[indent_line - 1])
    newline = "\r\n" if "\r\n" in text else "\n"

    text = (
        text[: end.end_offset] + newline + header_indent + "}" + text[end.end_offset :]
    )

    if start.line > brace_line:
        lines = text.splitlines(keepends=True)
        line = lines[brace_line - 1]
        stripped = line.rstrip("\r\n")
        ending = line[len(stripped) :]
        lines[brace_line - 1] = stripped.rstrip() + " {" + ending
        return "".join(lines)

    unit = "\t" if header_indent.startswith("\t") else "    "
    return (
        text[: start.offset]
        + "{"
        + newline
        + header_indent
        + unit
        + text[start.offset :]
    )


def function_definition_findings(
    tokens: list[Token],
    matches: dict[int, int],
    lines: list[str],
    directives: set[int],
    comment_lines: set[int],
) -> list[Finding]:
    findings: list[Finding] = []
    for open_index, close_index in matches.items():
        if tokens[open_index].text != "{":
            continue
        if not _is_function_body(tokens, matches, open_index):
            continue
        start = _declaration_start(tokens, matches, open_index)
        start_token = tokens[start]
        end_token = tokens[close_index]
        signature_line = start_token.line
        last_line = end_token.line
        if signature_line - 1 >= len(lines):
            continue
        if lines[signature_line - 1][: start_token.col - 1].strip():
            continue
        first_line = _leading_comment_start(lines, comment_lines, signature_line)
        if not _is_blank(lines, first_line - 1) and not _previous_is_separator(
            lines, directives, first_line
        ):
            findings.append(
                Finding(
                    "before",
                    first_line,
                    start_token.col,
                    "function",
                    start_line=first_line,
                    end_line=last_line,
                )
            )
        if not _is_blank(lines, last_line + 1) and not _next_is_separator(
            lines, directives, last_line
        ):
            findings.append(
                Finding(
                    "after",
                    last_line,
                    end_token.end_col,
                    "function",
                    start_line=first_line,
                    end_line=last_line,
                )
            )
    return findings


def _is_type_body(
    tokens: list[Token], matches: dict[int, int], open_index: int
) -> bool:
    cursor = open_index - 1
    while cursor >= 0:
        text = tokens[cursor].text
        if text in {"struct", "class", "enum", "union"}:
            return True
        if text in {";", "{", "}", "=", "(", ")", "->"}:
            return False
        if text in ")]":
            opener = matches.get(cursor)
            if opener is None:
                return False
            cursor = opener - 1
            continue
        cursor -= 1
    return False


def _declaration_start_simple(
    tokens: list[Token], matches: dict[int, int], open_index: int
) -> int:
    cursor = open_index - 1
    while cursor >= 0:
        text = tokens[cursor].text
        if text in ")]":
            opener = matches.get(cursor)
            if opener is None:
                return cursor + 1
            cursor = opener - 1
            continue
        if text in "([{":
            return cursor + 1
        if text in ";}":
            return cursor + 1
        if text == ":":
            if cursor > 0 and tokens[cursor - 1].text == ":":
                cursor -= 2
                continue
            if cursor > 0 and tokens[cursor - 1].text in {
                "public",
                "private",
                "protected",
                "case",
                "default",
            }:
                return cursor + 1
            cursor -= 1
            continue
        cursor -= 1
    return 0


def _definition_end(
    tokens: list[Token], matches: dict[int, int], close_index: int
) -> int:
    cursor = close_index + 1
    if cursor < len(tokens) and tokens[cursor].text == ";":
        return cursor
    depth = 0
    while cursor < len(tokens):
        text = tokens[cursor].text
        if text in "([{":
            depth += 1
        elif text in ")]}":
            if depth == 0:
                return close_index
            depth -= 1
        elif text == ";" and depth == 0:
            return cursor
        cursor += 1
    return close_index


def type_definition_findings(
    tokens: list[Token],
    matches: dict[int, int],
    lines: list[str],
    directives: set[int],
    comment_lines: set[int],
) -> list[Finding]:
    findings: list[Finding] = []
    for open_index, close_index in matches.items():
        if tokens[open_index].text != "{":
            continue
        if not _is_type_body(tokens, matches, open_index):
            continue
        start = _declaration_start_simple(tokens, matches, open_index)
        start_token = tokens[start]
        end_index = _definition_end(tokens, matches, close_index)
        end_token = tokens[end_index]
        signature_line = start_token.line
        last_line = end_token.line
        if signature_line - 1 >= len(lines):
            continue
        if lines[signature_line - 1][: start_token.col - 1].strip():
            continue
        first_line = _leading_comment_start(lines, comment_lines, signature_line)
        if not _is_blank(lines, first_line - 1) and not _previous_is_separator(
            lines, directives, first_line
        ):
            findings.append(
                Finding(
                    "before",
                    first_line,
                    start_token.col,
                    "type",
                    start_line=first_line,
                    end_line=last_line,
                )
            )
        if not _is_blank(lines, last_line + 1) and not _next_is_separator(
            lines, directives, last_line
        ):
            findings.append(
                Finding(
                    "after",
                    last_line,
                    end_token.end_col,
                    "type",
                    start_line=first_line,
                    end_line=last_line,
                )
            )
    return findings


def _is_namespace_body(tokens: list[Token], open_index: int) -> bool:
    cursor = open_index - 1
    while cursor >= 0:
        text = tokens[cursor].text
        if text == "namespace":
            return not (cursor > 0 and tokens[cursor - 1].text == "using")
        if text in {";", "{", "}", "(", ")", "="}:
            return False
        cursor -= 1
    return False


def redundant_semicolon_findings(
    tokens: list[Token], matches: dict[int, int]
) -> list[Finding]:
    findings: list[Finding] = []
    for index in range(1, len(tokens)):
        if tokens[index].text != ";" or tokens[index - 1].text != "}":
            continue
        close_index = index - 1
        open_index = matches.get(close_index)
        if open_index is None:
            continue
        if _is_type_body(tokens, matches, open_index):
            continue
        if _is_requires_body(tokens, matches, open_index):
            continue
        if _in_requires_body(tokens, matches, open_index):
            continue
        redundant = False
        if _is_function_body(tokens, matches, open_index):
            redundant = True
        elif _is_namespace_body(tokens, open_index):
            redundant = True
        else:
            previous = tokens[open_index - 1] if open_index > 0 else None
            if previous is not None:
                if previous.kind == "string":
                    redundant = True
                elif previous.text in {"else", "do", "try"}:
                    redundant = True
                elif previous.text in {";", "{", "}"}:
                    redundant = True
                elif previous.text == ")":
                    opener = matches.get(open_index - 1)
                    if opener is not None and opener > 0:
                        before = tokens[opener - 1].text
                        if before in CONTROL_PAREN_KEYWORDS:
                            redundant = True
                        elif (
                            before == "constexpr"
                            and opener > 1
                            and tokens[opener - 2].text == "if"
                        ):
                            redundant = True
        if not redundant:
            continue
        token = tokens[index]
        findings.append(
            Finding(
                "semicolon",
                token.line,
                token.col,
                start_line=token.line,
                end_line=token.line,
                token_index=index,
            )
        )
    return findings


def forever_loop_findings(
    tokens: list[Token],
    matches: dict[int, int],
    comment_ranges: list[tuple[int, int]],
) -> list[Finding]:
    findings: list[Finding] = []
    for index, token in enumerate(tokens):
        if token.text != "for":
            continue
        open_index = index + 1
        if open_index >= len(tokens) or tokens[open_index].text != "(":
            continue
        close = matches.get(open_index)
        if close is None or close != index + 4:
            continue
        if tokens[index + 2].text != ";" or tokens[index + 3].text != ";":
            continue
        open_token = tokens[open_index]
        close_token = tokens[close]
        if any(
            start < close_token.offset and end > open_token.end_offset
            for start, end in comment_ranges
        ):
            continue
        findings.append(
            Finding(
                "forever",
                token.line,
                token.col,
                start_line=token.line,
                end_line=close_token.end_line,
                token_index=index,
                close_index=close,
            )
        )
    return findings


def case_label_findings(
    tokens: list[Token],
    matches: dict[int, int],
    lines: list[str],
) -> list[Finding]:
    findings: list[Finding] = []
    seen: set[int] = set()
    for index, token in enumerate(tokens):
        if token.text != "switch":
            continue
        cursor = index + 1
        if cursor >= len(tokens) or tokens[cursor].text != "(":
            continue
        close = matches.get(cursor)
        if close is None:
            continue
        body = close + 1
        if body >= len(tokens) or tokens[body].text != "{":
            continue
        body_close = matches.get(body)
        if body_close is None:
            continue

        statements: list[tuple[int, int]] = []
        statement = body + 1
        while statement < body_close:
            end = statement_end(tokens, statement, body_close, matches)
            if end is None:
                statement += 1
                continue
            statements.append((statement, end))
            statement = end + 1

        open_line = tokens[body].line
        for start, _ in statements:
            if tokens[start].text not in {"case", "default"}:
                continue
            line = tokens[start].line
            if (
                line < len(lines)
                and not lines[line].strip()
                and line + 1 < tokens[body_close].line
                and line + 1 not in seen
            ):
                seen.add(line + 1)
                findings.append(Finding("case", line + 1, 1, action="after"))
            if (
                line - 1 > open_line
                and line - 2 >= 0
                and not lines[line - 2].strip()
                and line - 1 not in seen
            ):
                seen.add(line - 1)
                findings.append(Finding("case", line - 1, 1, action="before"))
    return findings


def _is_check_operand(token: Token) -> bool:
    return token.kind in {"number", "word", "string"}


def _simple_if_check(
    tokens: list[Token], matches: dict[int, int], statement: Statement
) -> str | None:
    index = statement.start_index
    if index >= len(tokens) or tokens[index].text != "if":
        return None
    cursor = index + 1
    if cursor < len(tokens) and tokens[cursor].text == "constexpr":
        cursor += 1
    if cursor >= len(tokens) or tokens[cursor].text != "(":
        return None
    close = matches.get(cursor)
    if close is None:
        return None
    condition = tokens[cursor + 1 : close]
    if len(condition) == 1 and condition[0].kind == "word":
        return condition[0].text
    if len(condition) == 2 and condition[0].text == "!" and condition[1].kind == "word":
        return condition[1].text
    if (
        len(condition) == 3
        and condition[0].kind == "word"
        and condition[1].text in {"<", ">"}
        and _is_check_operand(condition[2])
    ):
        return condition[0].text
    if (
        len(condition) == 4
        and condition[0].kind == "word"
        and condition[1].text in {"<", ">"}
        and condition[2].text == "="
        and _is_check_operand(condition[3])
    ):
        return condition[0].text
    if len(condition) != 4:
        return None
    first, second, third, fourth = condition
    if not (
        (second.text == "=" and third.text == "=")
        or (second.text == "!" and third.text == "=")
    ):
        return None
    if first.kind == "word" and _is_check_operand(fourth):
        return first.text
    if _is_check_operand(first) and fourth.kind == "word":
        return fourth.text
    return None


def _assigned_name(body: list[Token]) -> str | None:
    depth = 0
    for index, token in enumerate(body):
        if token.text in "([{":
            depth += 1
            continue
        if token.text in ")]}":
            depth -= 1
            continue
        if token.text != "=" or depth != 0:
            continue
        cursor = index - 1
        while (
            cursor >= 0
            and body[cursor].kind == "punct"
            and body[cursor].text in "+-*/%&|^<>"
        ):
            cursor -= 1
        if cursor >= 0 and body[cursor].kind == "word":
            return body[cursor].text
        return None
    return None


def namespace_findings(
    tokens: list[Token], matches: dict[int, int], lines: list[str]
) -> list[Finding]:
    findings: list[Finding] = []
    for index, token in enumerate(tokens):
        if token.text != "namespace":
            continue
        if index and tokens[index - 1].text == "using":
            continue
        cursor = index + 1
        while cursor < len(tokens) and tokens[cursor].text not in "{;":
            cursor += 1
        if cursor >= len(tokens) or tokens[cursor].text != "{":
            continue
        close = matches.get(cursor)
        if close is None:
            continue
        open_line = tokens[cursor].line
        close_line = tokens[close].line
        if open_line + 1 < close_line and not lines[open_line].strip():
            findings.append(Finding("namespace", open_line + 1, 1, action="open"))
        if (
            close_line - 1 > open_line
            and close_line - 1 != open_line + 1
            and not lines[close_line - 2].strip()
        ):
            findings.append(Finding("namespace", close_line - 1, 1, action="close"))
    return findings


def _is_blank(lines: list[str], line: int) -> bool:
    return 0 < line <= len(lines) and not lines[line - 1].strip()


def _only_comment(rest: str) -> bool:
    text = rest.strip()
    return (
        not text
        or text.startswith("//")
        or (text.startswith("/*") and text.endswith("*/"))
    )


def _previous_non_blank(lines: list[str], line: int) -> int | None:
    cursor = line - 1
    while cursor >= 1:
        if lines[cursor - 1].strip():
            return cursor
        cursor -= 1
    return None


def _next_non_blank(lines: list[str], line: int) -> int | None:
    cursor = line + 1
    while cursor <= len(lines):
        if lines[cursor - 1].strip():
            return cursor
        cursor += 1
    return None


def _previous_is_separator(lines: list[str], directives: set[int], line: int) -> bool:
    previous = _previous_non_blank(lines, line)
    if previous is None:
        return True
    if previous in directives:
        return True
    text = lines[previous - 1].strip()
    if text.startswith("#"):
        return True
    without_comments = re.sub(r"/\*.*?\*/", "", text)
    without_comments = re.sub(r"//.*", "", without_comments).rstrip()
    if without_comments.endswith("{"):
        return True
    return bool(LABEL_RE.match(text))


def _next_is_separator(lines: list[str], directives: set[int], line: int) -> bool:
    following = _next_non_blank(lines, line)
    if following is None:
        return True
    if following in directives:
        return True
    text = lines[following - 1].strip()
    if text.startswith("#") or text.startswith("}") or text.startswith(";"):
        return True
    return bool(LABEL_RE.match(text))


def _is_assignment(body: list[Token]) -> bool:
    if not body:
        return False
    if body[0].text in CONTROL_KEYWORDS or body[0].text in STATEMENT_KEYWORDS:
        return False
    if _is_declaration(body):
        return False
    depth = 0
    for index, token in enumerate(body):
        text = token.text
        if text in "([{":
            depth += 1
            continue
        if text in ")]}":
            depth -= 1
            continue
        if text != "=" or depth != 0:
            continue
        previous = body[index - 1].text if index > 0 else ""
        following = body[index + 1].text if index + 1 < len(body) else ""
        if following == "=" or previous in {"=", "!"}:
            continue
        if previous in {"<", ">"}:
            before_previous = body[index - 2].text if index > 1 else ""
            if before_previous != previous:
                continue
        return True
    return False


BUILTIN_SCALARS = frozenset(
    {
        "bool",
        "char",
        "char8_t",
        "char16_t",
        "char32_t",
        "double",
        "float",
        "int",
        "long",
        "short",
        "signed",
        "unsigned",
        "void",
        "wchar_t",
        "u8",
        "u16",
        "u32",
        "u64",
        "i8",
        "i16",
        "i32",
        "i64",
        "usz",
        "isz",
        "size_t",
        "ssize_t",
        "ptrdiff_t",
    }
)
DECLARATION_PREFIXES = frozenset(
    {
        "alignas",
        "class",
        "const",
        "consteval",
        "constexpr",
        "constinit",
        "enum",
        "explicit",
        "extern",
        "friend",
        "inline",
        "mutable",
        "register",
        "static",
        "struct",
        "thread_local",
        "typename",
        "union",
        "volatile",
    }
)


def _declaration_kind(body: list[Token]) -> str:
    depth = 0
    prefix_end = len(body)
    for index, token in enumerate(body):
        if token.text in "([{":
            depth += 1
        elif token.text in ")]}":
            depth -= 1
        elif token.text == "=" and depth == 0:
            prefix_end = index
            break
    prefix = body[:prefix_end]
    if any(token.text == "&" for token in prefix):
        return "reference"
    if any(token.text == "*" for token in prefix):
        return "object"
    index = 0
    while index < len(prefix) and prefix[index].text in DECLARATION_PREFIXES:
        index += 1
    if index < len(prefix) and prefix[index].text in BUILTIN_SCALARS:
        return "scalar"
    return "object"


def statement_group_findings(
    tokens: list[Token],
    matches: dict[int, int],
    blocks: list[list[tuple[int, int]]],
    lines: list[str],
    directives: set[int],
    comment_lines: set[int],
) -> list[Finding]:
    findings: list[Finding] = []
    for block in blocks:
        run_start: int | None = None
        run_end = 0
        run_kind: str | None = None
        run_keyword = ""
        for index, end in block:
            body = tokens[index : end + 1]
            if _is_declaration(body):
                kind = "declaration:" + _declaration_kind(body)
                keyword = "declaration"
            elif _is_assignment(body):
                kind = "assignment"
                keyword = "assignment"
            else:
                kind = None
                keyword = ""
            if kind is not None and kind == run_kind:
                run_end = end
                continue
            if run_kind is not None:
                findings.extend(
                    _group_findings(
                        tokens,
                        lines,
                        directives,
                        comment_lines,
                        run_keyword,
                        run_start,
                        run_end,
                    )
                )
            run_start = index if kind is not None else None
            run_end = end
            run_kind = kind
            run_keyword = keyword
        if run_kind is not None:
            findings.extend(
                _group_findings(
                    tokens,
                    lines,
                    directives,
                    comment_lines,
                    run_keyword,
                    run_start,
                    run_end,
                )
            )
    return findings


def _group_findings(
    tokens: list[Token],
    lines: list[str],
    directives: set[int],
    comment_lines: set[int],
    keyword: str,
    start: int,
    end: int,
) -> list[Finding]:
    signature_line = tokens[start].line
    last_line = tokens[end].line
    if signature_line - 1 >= len(lines):
        return []
    if lines[signature_line - 1][: tokens[start].col - 1].strip():
        return []
    if last_line - 1 >= len(lines):
        return []
    if not _only_comment(lines[last_line - 1][tokens[end].end_col - 1 :]):
        return []
    first_line = _leading_comment_start(lines, comment_lines, signature_line)

    findings: list[Finding] = []
    if not _is_blank(lines, first_line - 1) and not _previous_is_separator(
        lines, directives, first_line
    ):
        findings.append(
            Finding(
                "before",
                first_line,
                tokens[start].col,
                keyword,
                start_line=first_line,
                end_line=last_line,
            )
        )
    if not _is_blank(lines, last_line + 1) and not _next_is_separator(
        lines, directives, last_line
    ):
        findings.append(
            Finding(
                "after",
                last_line,
                tokens[end].end_col,
                keyword,
                start_line=first_line,
                end_line=last_line,
            )
        )
    return findings


def analyze(text: str, cxx: bool = True) -> tuple[list[Finding], list[Token]]:
    lines = text.splitlines()
    if not lines:
        return [], []
    tokens, directives, directive_ranges, comment_ranges, comment_lines = lex(text)
    if not tokens:
        return [], tokens
    matches = build_matches(tokens)
    blocks = block_statement_lists(tokens, matches)
    findings: list[Finding] = []
    checkable: list[Statement] = []

    for statement in collect_statements(tokens, matches, blocks):
        if statement.line - 1 >= len(lines):
            continue
        if lines[statement.line - 1][: statement.col - 1].strip():
            continue
        if statement.end_line - 1 >= len(lines):
            continue
        if not _only_comment(lines[statement.end_line - 1][statement.end_col - 1 :]):
            continue
        checkable.append(statement)

    control = [
        statement
        for statement in checkable
        if statement.keyword in CONTROL_KEYWORDS or statement.keyword == "block"
    ]
    returns = [
        statement for statement in checkable if statement.keyword in RETURN_KEYWORDS
    ]

    for statement in control:
        first_line = statement.line
        last_line = statement.end_line
        comment_start = _leading_comment_start(lines, comment_lines, first_line)
        if not _is_blank(lines, comment_start - 1) and not _previous_is_separator(
            lines, directives, comment_start
        ):
            findings.append(
                Finding(
                    "before",
                    first_line,
                    statement.col,
                    statement.keyword,
                    start_line=comment_start,
                    end_line=last_line,
                )
            )
        if not _is_blank(lines, last_line + 1) and not _next_is_separator(
            lines, directives, last_line
        ):
            findings.append(
                Finding(
                    "after",
                    last_line,
                    statement.end_col,
                    statement.keyword,
                    start_line=first_line,
                    end_line=last_line,
                )
            )

    for statement in returns:
        first_line = statement.line
        comment_start = _leading_comment_start(lines, comment_lines, first_line)
        if not _is_blank(lines, comment_start - 1) and not _previous_is_separator(
            lines, directives, comment_start
        ):
            findings.append(
                Finding(
                    "before",
                    first_line,
                    statement.col,
                    statement.keyword,
                    start_line=comment_start,
                    end_line=statement.end_line,
                )
            )

    findings.extend(
        statement_group_findings(
            tokens, matches, blocks, lines, directives, comment_lines
        )
    )
    findings.extend(
        brace_findings(tokens, matches, directive_ranges, comment_ranges, blocks)
    )
    findings.extend(loop_brace_findings(tokens, matches, blocks))
    findings.extend(
        function_definition_findings(tokens, matches, lines, directives, comment_lines)
    )
    findings.extend(
        type_definition_findings(tokens, matches, lines, directives, comment_lines)
    )
    findings.extend(namespace_findings(tokens, matches, lines))
    findings.extend(redundant_semicolon_findings(tokens, matches))
    findings.extend(case_label_findings(tokens, matches, lines))
    if cxx:
        findings.extend(forever_loop_findings(tokens, matches, comment_ranges))

    blank = [finding for finding in findings if finding.kind in ("before", "after")]
    others = [
        finding for finding in findings if finding.kind not in ("before", "after")
    ]
    seen: set[int] = set()
    deduped: list[Finding] = []
    for finding in sorted(
        blank,
        key=lambda finding: (
            finding.start_line - 1 if finding.kind == "before" else finding.end_line,
            0 if finding.kind == "before" else 1,
        ),
    ):
        position = (
            finding.start_line - 1 if finding.kind == "before" else finding.end_line
        )
        if position in seen:
            continue
        seen.add(position)
        deduped.append(finding)

    findings = others + deduped

    previous: dict[int, tuple[int, int]] = {}
    for block in blocks:
        for position in range(1, len(block)):
            previous[block[position][0]] = block[position - 1]

    exempt: set[int] = set()
    for statement in checkable:
        if statement.keyword != "if":
            continue
        name = _simple_if_check(tokens, matches, statement)
        if name is None:
            continue
        preceding = previous.get(statement.start_index)
        if preceding is None:
            continue
        body = tokens[preceding[0] : preceding[1] + 1]
        if not (_is_declaration(body) or _is_assignment(body)):
            continue
        if _assigned_name(body) != name:
            continue
        after_line = tokens[preceding[1]].end_line
        line = statement.line
        if line == after_line + 1:
            exempt.add(line - 1)
        elif (
            line == after_line + 2
            and after_line < len(lines)
            and not lines[after_line].strip()
        ):
            before_preceding = previous.get(preceding[0])
            if before_preceding is not None:
                before_body = tokens[before_preceding[0] : before_preceding[1] + 1]
                if _is_declaration(before_body) or _is_assignment(before_body):
                    continue
            findings.append(Finding("check", after_line + 1, 1, action="remove"))
    if exempt:
        findings = [
            finding
            for finding in findings
            if not (
                finding.kind in ("before", "after")
                and (
                    finding.start_line - 1
                    if finding.kind == "before"
                    else finding.end_line
                )
                in exempt
            )
        ]

    findings.sort(key=lambda finding: (finding.line, finding.col, finding.kind))
    return findings, tokens


def find_findings(text: str, cxx: bool = True) -> list[Finding]:
    return analyze(text, cxx)[0]


def _apply(text: str, tokens: list[Token], finding: Finding) -> str:
    if finding.kind in ("case", "check"):
        lines = text.splitlines(keepends=True)
        del lines[finding.line - 1]
        return "".join(lines)

    if finding.kind == "forever":
        assert finding.token_index is not None and finding.close_index is not None
        start = tokens[finding.token_index]
        end = tokens[finding.close_index]
        return text[: start.offset] + "while (true)" + text[end.end_offset :]

    if finding.kind == "semicolon":
        assert finding.token_index is not None
        token = tokens[finding.token_index]
        return text[: token.offset] + text[token.end_offset :]

    if finding.kind == "namespace":
        lines = text.splitlines(keepends=True)
        del lines[finding.line - 1]
        return "".join(lines)

    if finding.kind in ("before", "after"):
        newline = "\r\n" if "\r\n" in text else "\n"
        lines = text.splitlines(keepends=True)
        index = finding.start_line - 1 if finding.kind == "before" else finding.end_line
        lines.insert(index, newline)
        return "".join(lines)

    if finding.kind == "braces" and finding.action == "add":
        assert finding.body_start is not None and finding.body_end is not None
        return _apply_add_braces(text, tokens, finding)

    assert finding.kind == "braces"
    assert finding.open_index is not None and finding.close_index is not None
    assert finding.body_start is not None and finding.body_end is not None
    edits: list[tuple[int, int, str]] = []
    open_token = tokens[finding.open_index]
    close_token = tokens[finding.close_index]
    body_token = tokens[finding.body_start]
    end_token = tokens[finding.body_end]
    edits.append((end_token.end_offset, close_token.end_offset, ""))
    edits.append((open_token.offset, body_token.offset, ""))

    for start, end, replacement in sorted(
        edits, key=lambda edit: edit[0], reverse=True
    ):
        text = text[:start] + replacement + text[end:]
    return text


def fix_text(text: str, cxx: bool = True) -> tuple[str, int]:
    fixed = 0
    newline = "\r\n" if "\r\n" in text else "\n"
    while True:
        findings, tokens = analyze(text, cxx)
        if not findings:
            return text, fixed

        insertions = sorted(
            {
                finding.start_line - 1 if finding.kind == "before" else finding.end_line
                for finding in findings
                if finding.kind in ("before", "after")
            },
            reverse=True,
        )
        if insertions:
            lines = text.splitlines(keepends=True)
            for position in insertions:
                lines.insert(position, newline)
            text = "".join(lines)
            fixed += len(insertions)
            continue

        deletions = sorted(
            {
                finding.line
                for finding in findings
                if finding.kind in ("case", "check", "group", "namespace")
            },
            reverse=True,
        )
        if deletions:
            lines = text.splitlines(keepends=True)
            for line in deletions:
                del lines[line - 1]
            text = "".join(lines)
            fixed += len(deletions)
            continue

        text = _apply(text, tokens, findings[0])
        fixed += 1
        if fixed > 1_000_000:
            raise RuntimeError("style fix did not converge")


def _git(root: pathlib.Path, *args: str) -> bytes:
    return subprocess.check_output(["git", "-C", str(root), *args])


def _decode(raw: bytes) -> list[str]:
    return [
        item.decode("utf-8", "surrogateescape") for item in raw.split(b"\0") if item
    ]


def _is_commit(root: pathlib.Path, rev: str) -> bool:
    try:
        subprocess.check_output(
            ["git", "-C", str(root), "cat-file", "-e", f"{rev}^{{commit}}"],
            stderr=subprocess.DEVNULL,
        )
    except (OSError, subprocess.CalledProcessError):
        return False
    return True


def _resolve_scope(
    root: pathlib.Path, base: str | None, head: str | None
) -> tuple[str | None, str | None]:
    head = head or "HEAD"
    if not _is_commit(root, head):
        head = "HEAD"
    if base and _is_commit(root, base):
        if _is_commit(root, "origin/main"):
            main_commit = _git(root, "rev-parse", "origin/main").decode().strip()
            head_commit = _git(root, "rev-parse", head).decode().strip()
            if main_commit != head_commit:
                ancestor = subprocess.run(
                    [
                        "git",
                        "-C",
                        str(root),
                        "merge-base",
                        "--is-ancestor",
                        main_commit,
                        head_commit,
                    ],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
                if ancestor.returncode == 0:
                    return main_commit, head
        try:
            resolved = _git(root, "merge-base", base, head).decode().strip()
        except subprocess.CalledProcessError:
            resolved = ""
        return (resolved or base), head
    for ref in ("origin/main", "main"):
        if _is_commit(root, ref):
            try:
                resolved = _git(root, "merge-base", head, ref).decode().strip()
            except subprocess.CalledProcessError:
                continue
            if resolved:
                return resolved, head
    if _is_commit(root, "HEAD^"):
        return "HEAD^", head
    return None, head


def _is_vendored(root: pathlib.Path, path: pathlib.Path) -> bool:
    try:
        relative = path.resolve().relative_to(root).as_posix()
    except ValueError:
        relative = path.as_posix()
    return relative.startswith(VENDORED_PREFIXES)


def _source_files(root: pathlib.Path) -> list[pathlib.Path]:
    raw = _git(root, "ls-files", "-z", "--cached", "--others", "--exclude-standard")
    return [
        root / name
        for name in _decode(raw)
        if pathlib.Path(name).suffix in SOURCE_SUFFIXES
        and not name.startswith(VENDORED_PREFIXES)
    ]


def _changed_files(root: pathlib.Path, base: str, head: str) -> list[pathlib.Path]:
    raw = _git(
        root,
        "diff",
        "--name-only",
        "--diff-filter=ACMR",
        "-z",
        base,
        head,
        "--",
    )
    return [
        root / name
        for name in _decode(raw)
        if pathlib.Path(name).suffix in SOURCE_SUFFIXES
        and not name.startswith(VENDORED_PREFIXES)
    ]


def _added_lines(
    root: pathlib.Path, base: str, head: str, path: pathlib.Path
) -> set[int] | None:
    relative = path.relative_to(root).as_posix()
    try:
        output = _git(
            root, "diff", "--unified=0", "--no-color", base, head, "--", relative
        )
    except subprocess.CalledProcessError:
        return None
    added: set[int] = set()
    for match in re.finditer(
        r"^@@ -\d+(?:,(\d+))? \+(\d+)(?:,(\d+))? @@",
        output.decode("utf-8", "replace"),
        re.MULTILINE,
    ):
        start = int(match.group(2))
        count = int(match.group(3) or "1")
        if count == 0:
            added.add(start)
            added.add(start + 1)
        else:
            added.update(range(start, start + count))
    return added


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Check blank lines and single-statement braces."
    )
    parser.add_argument("files", nargs="*", help="explicit files to check")
    parser.add_argument("--root", default=".", help="repository root (default: .)")
    parser.add_argument("--base", help="base commit for changed-line scoping")
    parser.add_argument("--head", help="head commit for changed-line scoping")
    parser.add_argument(
        "--full-tree",
        action="store_true",
        help="check every tracked source file instead of changed files",
    )
    parser.add_argument(
        "--fix", action="store_true", help="apply the blank-line and brace fixes"
    )
    args = parser.parse_args(argv)

    root = pathlib.Path(args.root).resolve()
    scoped = False
    base, head = args.base, args.head

    try:
        if args.files:
            paths = [pathlib.Path(path) for path in args.files]
        else:
            if not args.full_tree:
                base, head = _resolve_scope(root, base, head)
                if base is None:
                    args.full_tree = True
            if args.full_tree:
                paths = _source_files(root)
            else:
                paths = _changed_files(root, base, head)
                scoped = True
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"cpp-style check: error: {error}", file=sys.stderr)
        return 2

    paths = [
        path
        for path in paths
        if path.suffix in SOURCE_SUFFIXES
        and path.is_file()
        and not _is_vendored(root, path)
    ]

    violations = 0
    changed_files = 0
    for path in paths:
        try:
            text = path.read_text(encoding="utf-8", errors="surrogateescape")
        except OSError as error:
            print(f"cpp-style check: error: {error}", file=sys.stderr)
            return 2

        if args.fix:
            fixed_text, fixed = fix_text(text, path.suffix in CXX_SUFFIXES)
            if fixed:
                path.write_text(fixed_text, encoding="utf-8", errors="surrogateescape")
                changed_files += 1
                print(f"{path}: applied {fixed} fix(es)")
            continue

        findings = find_findings(text, path.suffix in CXX_SUFFIXES)
        if scoped:
            added = _added_lines(root, base, head, path)
            if added is not None:
                findings = [
                    finding
                    for finding in findings
                    if any(
                        line in added
                        for line in range(finding.start_line - 1, finding.end_line + 2)
                    )
                ]

        for finding in findings:
            try:
                shown = path.relative_to(root)
            except ValueError:
                shown = path
            if finding.kind == "braces":
                if finding.action == "add":
                    if finding.keyword == "if":
                        message = "missing braces on this branch of the 'if' chain"
                    else:
                        message = f"missing braces around this '{finding.keyword}' body"
                else:
                    message = (
                        "unnecessary braces around this single-statement 'if' body"
                    )
            elif finding.kind == "namespace":
                if finding.action == "open":
                    message = "blank line after the namespace opening brace"
                else:
                    message = "blank line before the namespace closing brace"
            elif finding.kind == "semicolon":
                message = "redundant semicolon after this block"
            elif finding.kind == "forever":
                message = "rewrite this 'for (;;)' as 'while (true)'"
            elif finding.kind == "case":
                message = f"blank line {finding.action} this case label"
            elif finding.kind == "check":
                message = "remove the blank line before this check"
            else:
                subject = {
                    "declaration": "declaration group",
                    "assignment": "assignment group",
                    "function": "function definition",
                    "type": "type definition",
                    "case": "case label",
                    "block": "block",
                }.get(finding.keyword, f"'{finding.keyword}' statement")
                message = f"missing blank line {finding.kind} this {subject}"
            print(f"{shown}:{finding.line}:{finding.col}: error: {message}")
        violations += len(findings)

    if args.fix:
        print(
            f"cpp-style check: fixed {changed_files} file(s)"
            if changed_files
            else "cpp-style check: nothing to fix"
        )
        return 0

    if violations:
        print(
            f"cpp-style check: {violations} violation(s). Run "
            "'.github/scripts/check-cpp-style.py --fix <files>' to apply the fixes.",
            file=sys.stderr,
        )
        return 1

    print(f"cpp-style check: passed ({len(paths)} file(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
