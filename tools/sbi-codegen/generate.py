#!/usr/bin/env python3
"""tools/sbi-codegen: generates C++ DTOs + JSON (de)serializers from the R19
3GPP OpenAPI YAML in specs/5G_APIs-REL-19/.

Usage:
    python3 tools/sbi-codegen/generate.py <yaml-file>... --out <dir> [--commit <sha>]

Wired into CMake as a build step -- see libs/sbi-generated/CMakeLists.txt.
See docs/DECISIONS.md ADR-0010 for why this exists instead of
openapi-generator (its cpp-pistache-server model output does not compile for
3GPP's anyOf-open-enum pattern, and duplicates shared types across
independently-generated NFs).
"""

from __future__ import annotations

import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))

from sbi_codegen.loader import SchemaRegistry
from sbi_codegen.render import render
from sbi_codegen.schema_to_ir import Converter


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", help="YAML filenames (relative to --specs-dir)")
    parser.add_argument("--specs-dir", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument(
        "--commit",
        default="bca84b60a37773133bcae97e5c6c0d10a93b47b6",
        help="3GPP forge.3gpp.org commit the specs/ snapshot was taken from",
    )
    args = parser.parse_args()

    registry = SchemaRegistry(args.specs_dir)
    converter = Converter(registry)
    ir_types = converter.convert_files(args.files)

    written = render(ir_types, args.commit, args.out)

    kinds: dict[str, int] = {}
    for t in ir_types.values():
        kinds[type(t).__name__] = kinds.get(type(t).__name__, 0) + 1
    print(f"sbi-codegen: {len(ir_types)} types -> {len(written)} files in {args.out}", file=sys.stderr)
    print(f"sbi-codegen: breakdown {kinds}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
