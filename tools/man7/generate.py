#!/usr/bin/env python3
"""Generate static man7-style syscall pages from proc syscall sources."""

from __future__ import annotations

import argparse
import html
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
DEFAULT_CSS = ROOT / "nodivia.css"


def comments(text: str) -> list[tuple[str, str]]:
    pages: list[tuple[str, str]] = []
    pattern = re.compile(
        r"///\s*(?P<title>\w+)\s*-\s*(?P<body>.*?)(?=\n\s*///\s*\n|\n\s*///\s*\w+\s*-|\Z)",
        re.S,
    )
    for match in pattern.finditer(text):
        body = re.sub(r"^\s*///\s?", "", match.group("body"), flags=re.M).strip()
        pages.append((match.group("title"), body))
    return pages


def legacy_pages(text: str) -> list[tuple[str, str]]:
    names = re.findall(r"\bi64\s+sys_(\w+)\s*\(", text)
    return [(name, f"System call handler for `{name}`.\n\nThis page is generated from the kernel dispatcher source.")
            for name in names]


def render(name: str, body: str) -> str:
    paragraphs = []
    for block in body.split("\n\n"):
        lines = block.splitlines()
        if lines and all(line.startswith("    ") for line in lines):
            paragraphs.append("<pre>" + html.escape("\n".join(lines)) + "</pre>")
        else:
            paragraphs.append("<p>" + html.escape(block).replace("\n", "<br>\n") + "</p>")
    title = f"{name}(2)"
    return f"""<!doctype html>
<html lang=\"en\"><head><meta charset=\"utf-8\">
<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">
<title>{html.escape(title)} - Astia system call</title>
<link rel=\"stylesheet\" href=\"../nodivia.css\"></head>
<body><main class=\"surface surface__flex\">
<article class=\"surface__level__1 surface__minor\">
<h1>{html.escape(title)}</h1>
{''.join(paragraphs)}
</article></main></body></html>\n"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path, help="syscall source directory")
    parser.add_argument("output", type=Path, help="generated site directory")
    parser.add_argument("--css", type=Path, default=DEFAULT_CSS)
    args = parser.parse_args()

    pages: dict[str, str] = {}
    for path in sorted(args.source.rglob("*.hpp")):
        for name, body in comments(path.read_text()):
            pages[name] = body
    for path in sorted(args.source.rglob("*.cpp")):
        for name, body in legacy_pages(path.read_text()):
            pages.setdefault(name, body)

    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "nodivia.css").write_text(args.css.read_text())
    for name, body in sorted(pages.items()):
        (args.output / f"{name}.html").write_text(render(name, body))
    (args.output / "index.html").write_text(
        "<!doctype html><html><head><meta charset='utf-8'><title>Astia syscalls</title>"
        "<link rel='stylesheet' href='nodivia.css'></head><body><main class='surface'>"
        "<h1>Astia system calls</h1><ul>"
        + "".join(f"<li><a href='{html.escape(n)}.html'>{html.escape(n)}(2)</a></li>"
                 for n in sorted(pages))
        + "</ul></main></body></html>\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
