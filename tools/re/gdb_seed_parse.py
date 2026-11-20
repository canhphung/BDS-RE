#!/usr/bin/env python3
"""Call the original LevelSettings::parseSeedString and record its results.

    LD_LIBRARY_PATH=<openssl>/lib:. gdb -q -batch \
        -ex "py OUTPUT='/tmp/seed_cases.json'" \
        -x tools/re/gdb_seed_parse.py input/linux/bds/bedrock_server

Runs from input/linux/bds so libCrypto.so resolves. The server stops at main()
and nothing else runs. Every case builds a real std::string in the inferior and
calls the original 0x1c69910 with it; the observed return value is recorded.
"""
import json

import gdb

PARSE_SEED = 0x1c69910
SHA = "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad"

gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("break main")
gdb.execute("run")

# Scratch: a std::string object (32 bytes on this libstdc++) and a heap buffer.
string_object = int(gdb.parse_and_eval("(void*)malloc(0x40)"))
scratch = int(gdb.parse_and_eval("(void*)malloc(0x1000)"))
new_string = int(gdb.parse_and_eval("(void*)malloc(0x40)"))


def build_string(text):
    """Construct a std::string in the inferior and return its address."""
    payload = text.encode("utf-8")
    gdb.selected_inferior().write_memory(scratch, payload + b"\0")
    # Offsets follow the Itanium libstdc++ std::string layout at -O2:
    # +0 data pointer, +8 length, +16 inline buffer (16 bytes, SSO).
    if len(payload) <= 15:
        pointer = string_object + 16
        gdb.selected_inferior().write_memory(string_object, (pointer).to_bytes(8, "little"))
        gdb.selected_inferior().write_memory(string_object + 8, len(payload).to_bytes(8, "little"))
        gdb.selected_inferior().write_memory(string_object + 16, payload + b"\0")
    else:
        gdb.selected_inferior().write_memory(string_object, scratch.to_bytes(8, "little"))
        gdb.selected_inferior().write_memory(string_object + 8, len(payload).to_bytes(8, "little"))
        gdb.selected_inferior().write_memory(string_object + 16, len(payload).to_bytes(8, "little"))
    return string_object


def parse_seed(text, default):
    address = build_string(text)
    value = gdb.parse_and_eval(
        f"((unsigned int(*)(void const*, unsigned int)){PARSE_SEED:#x})((void*){address:#x}, {default})"
    )
    return int(value)


CASES = [
    # (input, default seed, note)
    ("", 0, "empty string returns the default"),
    ("", 12345, "empty string returns the non-zero default"),
    ("-", 0, "single dash is shorter than 2: default"),
    ("-", 7, "single dash returns the default"),
    ("12", 0, "two-digit integral"),
    ("123", 0, "three-digit integral"),
    ("0", 0, "single zero is shorter than 2: default"),
    ("00", 0, "leading zero is not integral: hashed"),
    ("01", 0, "leading zero is not integral: hashed"),
    ("-0", 0, "minus zero hashed (length 2, isIntegral true, but value == -1 check)"),
    ("-5", 0, "negative integral"),
    ("-123456", 0, "larger negative integral"),
    ("1", 0, "single digit returns the default (length < 2)"),
    ("9", 5, "single digit returns the non-zero default"),
    (" 123", 0, "leading space is trimmed before the checks"),
    ("123 ", 0, "trailing space is trimmed"),
    ("  -42  ", 0, "both sides trimmed"),
    ("\t7\n", 3, "tabs and newlines are trimmed"),
    ("abc", 0, "non-integral text is hashed"),
    ("1", 99, "seed \"1\" with a non-zero default"),
    ("-1", 0, "\"-1\" is the sentinel: hashed instead of parsed"),
    ("-1", 77, "\"-1\" hashed even with a non-zero default"),
    ("2147483647", 0, "int max"),
    ("-2147483648", 0, "int min"),
    ("2147483648", 0, "overflowing decimal is integral but out of range"),
    ("99999999999999999999", 0, "huge decimal"),
    ("0x10", 0, "hex text is not integral (leading 0) so hashed"),
    ("12a", 0, "mixed text is hashed"),
    ("1.5", 0, "decimal point text is hashed"),
    ("bedrock", 0, "ordinary word"),
    ("Bedrock level", 0, "text with a space inside"),
    ("hello world", 0, "two words"),
    ("日本語", 0, "multi-byte UTF-8 is hashed by byte"),
    ("-9", 0, "any two-character string starting with '-' returns the default"),
    ("-9", 55, "two-character dash string returns the non-zero default"),
    ("-ab", 0, "dash plus letter, length 3, not integral: hashed"),
    ("  -1", 0, "leading spaces then -1 trims to -1 and is accepted"),
    ("  -1", 33, "leading spaces then -1 with a non-zero default"),
    (" -1 ", 0, "padded -1"),
    ("-01", 0, "dash then leading zero: hashed"),
    ("-0", 44, "two-character -0 returns the default"),
    ("07", 0, "two-digit leading zero: hashed"),
    ("1 ", 0, "digit plus trailing space trims to one character"),
    ("x", 8, "single non-digit returns the default"),
    ("-", 9, "single dash returns the default"),
    ("2147483648", 0, "glibc sscanf wraps to INT_MIN"),
    ("-2147483649", 0, "glibc sscanf wraps to INT_MAX"),
    ("99999999999999999999", 0, "glibc sscanf wraps to -1 then is rejected"),
    ("-99999999999999999999", 11, "glibc sscanf wraps to 0 then is accepted"),
]

results = []
for text, default, note in CASES:
    value = parse_seed(text, default)
    results.append({"input": text, "default": default,
                    "signed": value, "unsigned": value & 0xFFFFFFFF,
                    "note": note})

payload = {
    "binary_sha256": SHA,
    "function": "LevelSettings::parseSeedString(std::string const&, unsigned int)",
    "address": "0x1c69910",
    "method": "the original function is called in the paused server with a "
              "std::string built by the oracle; results are native observations",
    "helper": "Util::hashCode 0x1949300 (h = 31*h + (signed char)c), "
              "Util::isIntegral 0x194a340, Util::stringTrim 0x270fa50 (' \\t\\n\\r')",
    "cases": results,
}
open(__import__("os").environ["OUTPUT"], "w").write(json.dumps(payload, indent=1) + "\n")
print(json.dumps({"cases": len(results)}))
