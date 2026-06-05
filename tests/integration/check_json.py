#!/usr/bin/env python3
"""Validate sysglance --json output against the documented schema.

Usage: check_json.py FILE [expected-module-order-comma-separated]
Exits non-zero (with a message) on any violation.
"""
import json
import sys


def fail(msg):
    print("schema error: " + msg, file=sys.stderr)
    sys.exit(1)


def main():
    with open(sys.argv[1], encoding="utf-8") as f:
        doc = json.load(f)
    if not isinstance(doc, dict):
        fail("top level is not an object")
    if set(doc) != {"sysglance", "modules"}:
        fail("unexpected top-level keys: %r" % sorted(doc))
    if not isinstance(doc["sysglance"], str) or doc["sysglance"].count(".") != 2:
        fail("bad version string")
    mods = doc["modules"]
    if not isinstance(mods, list) or not mods:
        fail("modules must be a non-empty list")
    for m in mods:
        if set(m) != {"name", "available", "entries"}:
            fail("module keys: %r" % sorted(m))
        if not isinstance(m["name"], str) or not m["name"]:
            fail("module name")
        if not isinstance(m["available"], bool):
            fail("available must be bool")
        if not isinstance(m["entries"], list):
            fail("entries must be list")
        if m["available"] != bool(m["entries"]):
            fail("available/entries mismatch in %s" % m["name"])
        for e in m["entries"]:
            if set(e) != {"label", "value", "data"}:
                fail("entry keys in %s: %r" % (m["name"], sorted(e)))
            if not isinstance(e["label"], str) or not isinstance(e["value"], str):
                fail("label/value must be strings in %s" % m["name"])
            if not isinstance(e["data"], dict):
                fail("data must be an object in %s" % m["name"])
            for k, v in e["data"].items():
                if not isinstance(v, (str, int, float)) and v is not None:
                    fail("data.%s in %s has type %s" % (k, m["name"], type(v).__name__))
    if len(sys.argv) > 2:
        want = sys.argv[2].split(",")
        got = [m["name"] for m in mods]
        if got != want:
            fail("module order %r != %r" % (got, want))


if __name__ == "__main__":
    main()
