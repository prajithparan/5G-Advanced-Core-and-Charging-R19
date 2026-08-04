#!/usr/bin/env python3
"""Structural schema-conformance check for the JSON samples written by
tests/conformance/test_round_trip.cpp, checked against the *actual* R19
OpenAPI schema (specs/5G_APIs-REL-19/), not a hand-copied expectation.

Scope, disclosed rather than assumed: this checks field-name/required-ness
structural conformance (every instance key is a declared property, every
required property is present) using the real $ref-resolving loader from
tools/sbi-codegen. It does NOT do full JSON Schema type/pattern/format
validation -- pattern enforcement is instead exercised directly in the C++
test (AmfIdPatternValidation) against the generated validate_AmfId function.
A fuller jsonschema-library-based validation (translating OpenAPI's
`nullable: true` to a real JSON Schema dialect) is future work, not silently
assumed done here.

Run as a CTest test via tests/conformance/CMakeLists.txt, after the C++
round-trip test has written its sample files.
"""

from __future__ import annotations

import json
import pathlib
import sys

_REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(_REPO_ROOT / "tools" / "sbi-codegen"))

from sbi_codegen.loader import SchemaRegistry  # noqa: E402


def check_object(schema: dict, instance: dict, registry: SchemaRegistry, from_file: str, path: str) -> list[str]:
    errors = []
    required = set(schema.get("required", []))
    properties = schema.get("properties", {})

    for req in required:
        if req not in instance:
            errors.append(f"{path}: missing required property '{req}'")

    for key, value in instance.items():
        if key not in properties:
            errors.append(f"{path}: property '{key}' is not declared in the schema")
            continue
        prop_schema = properties[key]
        if "$ref" in prop_schema and isinstance(value, dict):
            name, resolved, source_file = registry.resolve_ref(prop_schema["$ref"], from_file)
            errors.extend(check_object(resolved, value, registry, source_file, f"{path}.{key}"))

    return errors


def main() -> int:
    specs_dir = _REPO_ROOT / "specs" / "5G_APIs-REL-19"
    samples_dir = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(".")

    registry = SchemaRegistry(specs_dir)
    registry.load_file("TS29571_CommonData.yaml")
    registry.load_file("TS29510_Nnrf_NFManagement.yaml")

    all_errors: list[str] = []

    guami_path = samples_dir / "guami.json"
    if not guami_path.exists():
        print(f"FAIL: {guami_path} not found -- did the C++ round-trip test run first?", file=sys.stderr)
        return 1
    guami_instance = json.loads(guami_path.read_text())
    guami_schema, _ = registry.schemas["Guami"]
    all_errors.extend(
        check_object(guami_schema, guami_instance, registry, "TS29571_CommonData.yaml", "Guami")
    )

    # NFType: anyOf[{enum:[...]}, {type: string}] means ANY string is schema-valid
    # by design (3GPP's open/extensible enum pattern) -- so the "unknown value"
    # sample is expected to be schema-conformant, not a violation. This is the
    # exact case openapi-generator's generated model could not even represent
    # (see ADR-0010); asserting it round-trips *and* is schema-valid is the point.
    nftype_path = samples_dir / "nftype_unknown.json"
    if not nftype_path.exists():
        print(f"FAIL: {nftype_path} not found", file=sys.stderr)
        return 1
    nftype_value = json.loads(nftype_path.read_text())
    nftype_schema, _ = registry.schemas["NFType"]
    any_of = nftype_schema.get("anyOf", [])
    open_string_branch = any(m.get("type") == "string" and "enum" not in m for m in any_of)
    if not (open_string_branch and isinstance(nftype_value, str)):
        all_errors.append("NFType: expected an anyOf[enum,string] schema accepting any string")

    if all_errors:
        print("FAIL: structural conformance violations:", file=sys.stderr)
        for e in all_errors:
            print(f"  - {e}", file=sys.stderr)
        return 1

    print("PASS: all samples structurally conform to specs/5G_APIs-REL-19/TS29571_CommonData.yaml")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
