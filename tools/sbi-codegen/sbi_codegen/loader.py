"""Loads 3GPP OpenAPI YAML files and resolves $ref (internal and external,
cross-file) into a single schema registry keyed by type name.

3GPP's OpenAPI files consistently reuse the same schema name for the same
concept across files (e.g. every NF's YAML references
TS29571_CommonData.yaml#/components/schemas/NfInstanceId the same way), so
de-duplicating by name across files is safe and is exactly what avoids the
duplicate-type problem measured against openapi-generator in ADR-0010.
"""

from __future__ import annotations

import pathlib

import yaml


class SchemaRegistry:
    def __init__(self, specs_dir: pathlib.Path):
        self.specs_dir = specs_dir
        self._raw_docs: dict[str, dict] = {}  # filename -> parsed YAML doc
        self.schemas: dict[str, tuple[dict, str]] = {}  # name -> (schema, source_file)

    def _load_doc(self, filename: str) -> dict:
        if filename not in self._raw_docs:
            path = self.specs_dir / filename
            with open(path, encoding="utf-8") as f:
                self._raw_docs[filename] = yaml.safe_load(f)
        return self._raw_docs[filename]

    def load_file(self, filename: str) -> None:
        """Registers every schema defined in this file's components.schemas,
        without yet resolving $refs (that happens lazily via resolve_ref)."""
        doc = self._load_doc(filename)
        for name, schema in (doc.get("components", {}).get("schemas") or {}).items():
            if name not in self.schemas:
                self.schemas[name] = (schema, filename)

    def resolve_ref(self, ref: str, from_file: str) -> tuple[str, dict, str]:
        """Resolves a $ref string (internal '#/components/schemas/X' or
        external 'OtherFile.yaml#/components/schemas/X') to
        (type_name, schema_dict, source_file). Loads and registers the
        target file's schemas as a side effect if it's an external ref not
        yet seen."""
        if "#" not in ref:
            raise ValueError(f"unsupported $ref with no fragment: {ref}")
        file_part, frag = ref.split("#", 1)
        target_file = file_part if file_part else from_file
        if not frag.startswith("/components/schemas/"):
            raise ValueError(f"unsupported $ref fragment shape: {ref}")
        name = frag[len("/components/schemas/") :]

        if name not in self.schemas:
            self.load_file(target_file)

        if name not in self.schemas:
            raise KeyError(f"$ref target '{name}' not found after loading {target_file}")
        schema, source_file = self.schemas[name]
        return name, schema, source_file
