#!/usr/bin/env python3
"""Reassemble lifecycle log records split by interleaved kernel log output."""

import re
import sys

PREFIX = re.compile(r"\[[ 0-9]+\.[0-9]{6} +(?:DEBUG|INFO|WARN|ERROR|PANIC)\] ")
MARKERS = (
    "init launched pid=",
    "init exited pid=",
    "init terminated",
    "init launch failed",
)


def _has_marker(text: str) -> bool:
    return any(marker in text for marker in MARKERS)


def _lines(body: str) -> list[str]:
    """Split on '\\n' only, keeping the terminator on every finished line."""
    parts = body.split("\n")
    lines = [part + "\n" for part in parts[:-1]]
    if parts[-1]:
        lines.append(parts[-1])
    return lines


def normalize(text: str) -> str:
    matches = list(PREFIX.finditer(text))
    if not matches:
        return text

    # One record per prefix, in the order the prefixes appeared.
    records: list[str] = []
    # Records whose first line was cut off by an interleaved record, most
    # recently started last: a continuation line belongs to that one first.
    open_records: list[int] = []

    for index, match in enumerate(matches):
        start = match.end()
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        lines = _lines(text[start:end])

        first = lines[0] if lines else ""
        records.append(match.group(0) + first)
        current = len(records) - 1
        if not first.endswith("\n"):
            open_records.append(current)

        for line in lines[1:]:
            if open_records:
                target = open_records.pop()
                records[target] += line
                if not line.endswith("\n"):
                    open_records.append(target)
                continue
            # Nothing was interrupted, so the newline that ended the record
            # above was itself injected: this line continues that record.
            if _has_marker(records[current]):
                continue
            if records[current].endswith("\n"):
                records[current] = records[current][:-1]
            records[current] += line

    output: list[str] = []
    for record in records:
        if not _has_marker(record):
            continue
        if not record.endswith("\n"):
            record += "\n"
        output.append(record)
    return "".join(output)


if __name__ == "__main__":
    sys.stdout.write(normalize(sys.stdin.read()))
