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


def normalize(text: str) -> str:
    matches = list(PREFIX.finditer(text))
    if not matches:
        return text

    segments = []
    if matches[0].start() != 0:
        segments.append((None, text[: matches[0].start()]))

    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        segments.append((match, text[match.end() : end]))

    output: list[str] = []
    last_target: int | None = None
    for match, body in segments:
        if match is None:
            if last_target is not None:
                output[last_target] += body
            continue

        if any(marker in body for marker in MARKERS):
            output.append(match.group(0) + body)
            last_target = len(output) - 1
            continue

        # A foreign record may interleave after a target record's opening
        # fragment. Its first line belongs to that foreign message; any later
        # lines are continuations of the preceding target record.
        newline = body.find("\n")
        if newline < 0:
            if last_target is not None:
                output[last_target] += body
        elif last_target is not None:
            output[last_target] += body[newline + 1 :]

    return "".join(output)


if __name__ == "__main__":
    sys.stdout.write(normalize(sys.stdin.read()))
