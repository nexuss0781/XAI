#!/usr/bin/env python3
"""End-to-end Unicode source-encoding tests for xai_text_adapter."""

import json
import subprocess
import sys


def run(adapter, payload, *options):
    return subprocess.run(
        [adapter, *options, "-"],
        input=payload,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def check_success(adapter, payload, expected, *options):
    result = run(adapter, payload, *options)
    require(result.returncode == 0, f"adapter rejected valid input: {result.stderr!r}")
    record = json.loads(result.stdout.decode("utf-8"))
    content = record["result"]["content"]
    require(content == expected, "decoded content did not preserve Unicode text")
    require(record["result"]["content_bytes"] == len(expected.encode("utf-8")),
            "content_bytes is not the canonical UTF-8 byte length")
    require(record["result"]["media_type"] == "text/plain; charset=utf-8",
            "serialized content is not labeled as UTF-8")


def main():
    adapter = sys.argv[1]
    text = "A😀\x00\nאב e\u0301"
    check_success(adapter, b"\xef\xbb\xbf" + text.encode("utf-8"), text)
    check_success(adapter, text.encode("utf-16-le"), text, "--encoding", "utf-16le")
    check_success(adapter, b"\xff\xfe" + text.encode("utf-16-le"), text)
    check_success(adapter, text.encode("utf-16-be"), text, "--encoding", "utf-16be")
    check_success(adapter, b"\xfe\xff" + text.encode("utf-16-be"), text)
    check_success(adapter, text.encode("utf-32-le"), text, "--encoding", "utf-32le")
    check_success(adapter, b"\xff\xfe\x00\x00" + text.encode("utf-32-le"), text)
    check_success(adapter, text.encode("utf-32-be"), text, "--encoding", "utf-32be")
    check_success(adapter, b"\x00\x00\xfe\xff" + text.encode("utf-32-be"), text)
    check_success(adapter, text.encode("utf-8"), text)

    invalid_inputs = [
        (b"\xff\xfe\x00\xd8", ()),  # Unpaired UTF-16 high surrogate.
        (b"\x00\x11\x00\x00", ("--encoding", "utf-32be")),  # Above U+10FFFF.
        (b"\xff\xfe" + b"A\x00", ("--encoding", "utf-16be")),  # BOM mismatch.
        (b"\xf0\x9f", ()),  # Truncated BOM-less UTF-8.
    ]
    for payload, options in invalid_inputs:
        result = run(adapter, payload, *options)
        require(result.returncode == 2, f"malformed Unicode should fail with invalid_schema: {result.returncode}")
        require(b"malformed_or_mismatched_unicode_encoding" in result.stderr or
                b"well-formed UTF-8" in result.stderr,
                "malformed-input diagnostic is missing or unexpectedly detailed")
        require(b"A" not in result.stderr, "diagnostic unexpectedly echoed input text")

    print("PASS text adapter CLI: UTF-8/16/32 encodings, BOMs, controls, and malformed sequences")


if __name__ == "__main__":
    main()
