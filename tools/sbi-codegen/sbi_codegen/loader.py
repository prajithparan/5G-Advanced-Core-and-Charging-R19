"""Loads 3GPP OpenAPI YAML files and resolves $ref (internal and external,
cross-file) into a schema registry keyed by (source file, type name).

Earlier version of this file keyed the registry by name alone, on the
assumption that "3GPP's OpenAPI files consistently reuse the same schema name
for the same concept across files". That assumption is false in general: it
holds for genuinely shared TS29571_CommonData.yaml-style types referenced via
explicit cross-file $ref, but NOT for schemas two files each define locally
under the same name by coincidence. Checked against the actual R19 YAML
(2026-08-05): of the 5 pilot files, SubscriptionData (Nnrf_NFManagement vs.
Namf_Communication), NFProfile/NFService (Nnrf_NFManagement vs.
Nnrf_NFDiscovery), and Ipv4AddressRange/Ipv6PrefixRange/TransportProtocol/
MbsSession (Nnrf_NFManagement vs. TS29571_CommonData) are all genuinely
different schemas that happen to share a name -- the name-only registry
silently dropped every one of these local redefinitions (first file loaded
wins, no warning), which meant e.g. AMF's real SubscriptionData
(amfStatusUri/guamiList) never reached codegen at all; only NRF's
same-named-but-unrelated SubscriptionData (nfStatusNotificationUri/...) was
ever emitted. See docs/DECISIONS.md ADR-0017. Disambiguation into distinct
C++ type names happens downstream in schema_to_ir.py's Converter, which is
where the actual name collision (as opposed to storage/resolution
correctness, fixed here) gets resolved.
"""

from __future__ import annotations

import pathlib

import yaml


def pure_ref_target(schema: dict) -> str | None:
    """If schema is nothing but `{"$ref": X}` -- a pure indirection, not a real
    shape of its own -- returns X, else None. Real pattern in the R19 YAML
    (TS29505_Subscription_Data.yaml re-exports ~25 of TS29503_Nudm_UECM.yaml's
    and TS29503_Nudm_SDM.yaml's schemas verbatim by reference under its own
    local names, e.g. `SmfRegistration: {$ref: 'TS29503_Nudm_UECM.yaml#/
    components/schemas/SmfRegistration'}`) -- see docs/DECISIONS.md ADR-0024."""
    if isinstance(schema, dict) and set(schema.keys()) == {"$ref"}:
        return schema["$ref"]
    return None


class SchemaRegistry:
    def __init__(self, specs_dir: pathlib.Path):
        self.specs_dir = specs_dir
        self._raw_docs: dict[str, dict] = {}  # filename -> parsed YAML doc
        self._loaded_files: set[str] = set()
        self.schemas: dict[tuple[str, str], dict] = {}  # (source_file, name) -> schema

    def _load_doc(self, filename: str) -> dict:
        if filename not in self._raw_docs:
            path = self.specs_dir / filename
            with open(path, encoding="utf-8") as f:
                self._raw_docs[filename] = yaml.safe_load(f)
        return self._raw_docs[filename]

    def load_file(self, filename: str) -> None:
        """Registers every schema defined in this file's components.schemas
        under its own (filename, name) key -- always, even if some other
        file already defined a schema under the same bare name -- without yet
        resolving $refs (that happens lazily via resolve_ref). Idempotent:
        a file is only ever parsed/registered once."""
        if filename in self._loaded_files:
            return
        self._loaded_files.add(filename)
        doc = self._load_doc(filename)
        for name, schema in (doc.get("components", {}).get("schemas") or {}).items():
            self.schemas[(filename, name)] = schema

    def _resolve_ref_one_hop(self, ref: str, from_file: str) -> tuple[str, dict, str]:
        if "#" not in ref:
            raise ValueError(f"unsupported $ref with no fragment: {ref}")
        file_part, frag = ref.split("#", 1)
        target_file = file_part if file_part else from_file
        if not frag.startswith("/components/schemas/"):
            raise ValueError(f"unsupported $ref fragment shape: {ref}")
        name = frag[len("/components/schemas/") :]

        self.load_file(target_file)

        key = (target_file, name)
        if key not in self.schemas:
            raise KeyError(f"$ref target '{name}' not found in {target_file}")
        return name, self.schemas[key], target_file

    def resolve_ref(self, ref: str, from_file: str) -> tuple[str, dict, str]:
        """Resolves a $ref string (internal '#/components/schemas/X' or
        external 'OtherFile.yaml#/components/schemas/X') to
        (type_name, schema_dict, source_file). An internal ref always
        resolves within from_file's own namespace -- it can never accidentally
        pick up a same-named schema some other file happened to load first.
        Loads and registers the target file's schemas as a side effect if not
        already loaded.

        Transparently follows pure-$ref indirection schemas (see
        pure_ref_target) to their real target, so a caller always gets back
        an actual shape to convert, never a bare pass-through wrapper --
        otherwise every one of TS29505_Subscription_Data.yaml's re-exported
        names would generate as a spurious, disconnected OpaqueType instead
        of correctly resolving to the same type UDM already generates. See
        docs/DECISIONS.md ADR-0024.
        """
        name, schema, target_file = self._resolve_ref_one_hop(ref, from_file)
        seen = {(target_file, name)}
        while True:
            inner_ref = pure_ref_target(schema)
            if inner_ref is None:
                return name, schema, target_file
            name, schema, target_file = self._resolve_ref_one_hop(inner_ref, target_file)
            key = (target_file, name)
            if key in seen:
                raise ValueError(f"circular pure-$ref alias chain detected at {key}")
            seen.add(key)
