"""Converts raw OpenAPI schema dicts into the IR (ir.py), pulling in the
transitive closure of $ref'd types (from any file) via a worklist so each
named type is converted exactly once regardless of how many pilot files
reference it.
"""

from __future__ import annotations

from .ir import AliasType, FieldIR, IRType, ObjectType, OpaqueType, OpenEnumType, TypeRef
from .loader import SchemaRegistry

_PRIMITIVE_MAP = {
    "string": "std::string",
    "boolean": "bool",
    "integer": "std::int64_t",
    "number": "double",
}

# C++ keywords (including the alternative-operator spellings like 'and'/'or',
# easy to forget but reserved) that collide with legitimate 3GPP field/schema
# names observed in the R19 YAML.
_CPP_RESERVED = {
    "and", "and_eq", "bitand", "bitor", "class", "compl", "delete", "new",
    "not", "not_eq", "operator", "or", "or_eq", "template", "xor", "xor_eq",
}


def _cpp_field_name(json_name: str) -> str:
    # 3GPP JSON field names are lowerCamelCase but some are legitimately
    # digit-led (5qi, 5gMmCapability -- 5G QoS Identifier et al.), which is
    # not a valid C++ identifier start. Prefix with 'n' (kept lowercase to
    # match the surrounding camelCase convention) rather than reject/guess a
    # different name -- this is purely a C++-identifier-legality shim, the
    # JSON wire name (json_name) is untouched and still what's serialized.
    if json_name[:1].isdigit():
        json_name = "n" + json_name
    if json_name in _CPP_RESERVED:
        return json_name + "_"
    return json_name


def cpp_type_name(schema_name: str) -> str:
    """Sanitizes a 3GPP schema name (e.g. '5GDdnmfInfo', '2G3GLocationArea')
    into a valid C++ type identifier. Digit-led names get an 'N' prefix
    (matching the surrounding PascalCase convention) -- applied consistently
    everywhere a schema name becomes a C++ type name (both at the type's own
    declaration and at every reference to it), so they always agree. The
    lookup key used internally (registry/worklist/result dict) stays the
    original, unsanitized YAML name -- only the emitted C++ identifier
    changes. See docs/DECISIONS.md ADR-0010."""
    if schema_name[:1].isdigit():
        return "N" + schema_name
    if schema_name in _CPP_RESERVED:
        return schema_name + "_"
    return schema_name


def _bare_type_name(type_ref: TypeRef) -> str | None:
    """Unwraps array nesting to find the named-type C++ identifier a TypeRef
    ultimately points at (None for plain primitives)."""
    while type_ref.kind == "array":
        type_ref = type_ref.array_of
    return type_ref.cpp_name if type_ref.kind == "ref" else None


def _is_pattern_only(member: dict) -> bool:
    return set(member.keys()) <= {"pattern"} and "pattern" in member


def _is_open_enum_anyof(schema: dict) -> list[str] | None:
    any_of = schema.get("anyOf")
    if not isinstance(any_of, list) or len(any_of) != 2:
        return None
    enum_member = next((m for m in any_of if isinstance(m, dict) and "enum" in m), None)
    open_member = next(
        (m for m in any_of if isinstance(m, dict) and "enum" not in m and m.get("type") == "string"),
        None,
    )
    if enum_member is not None and open_member is not None:
        return list(enum_member["enum"])
    return None


class Converter:
    def __init__(self, registry: SchemaRegistry):
        self.registry = registry
        self.result: dict[str, IRType] = {}
        self._worklist: list[tuple[str, dict, str]] = []
        self._queued: set[str] = set()

    def convert_files(self, filenames: list[str]) -> dict[str, IRType]:
        for filename in filenames:
            self.registry.load_file(filename)
            for name, (schema, source_file) in list(self.registry.schemas.items()):
                if source_file == filename:
                    self._enqueue(name, schema, source_file)

        while self._worklist:
            name, schema, source_file = self._worklist.pop(0)
            if name in self.result:
                continue
            self.result[name] = self._convert_one(name, schema, source_file)

        return self.result

    def _enqueue(self, name: str, schema: dict, source_file: str) -> None:
        if name in self._queued:
            return
        self._queued.add(name)
        self._worklist.append((name, schema, source_file))

    def _resolve_ref_typeref(self, ref: str, from_file: str) -> TypeRef:
        name, schema, source_file = self.registry.resolve_ref(ref, from_file)
        self._enqueue(name, schema, source_file)
        return TypeRef(kind="ref", cpp_name=cpp_type_name(name))

    def _property_type_ref(self, prop_schema: dict, from_file: str, context_name: str) -> TypeRef:
        if "$ref" in prop_schema:
            return self._resolve_ref_typeref(prop_schema["$ref"], from_file)

        if prop_schema.get("type") == "array":
            items = prop_schema.get("items", {})
            inner = self._property_type_ref(items, from_file, context_name)
            return TypeRef(kind="array", cpp_name="", array_of=inner)

        ptype = prop_schema.get("type")
        if ptype in _PRIMITIVE_MAP and "enum" not in prop_schema and "anyOf" not in prop_schema:
            return TypeRef(kind="primitive", cpp_name=_PRIMITIVE_MAP[ptype])

        # Inline enum, anyOf, or other composed shape on a property: not
        # confidently modeled inline. Fall back to opaque JSON for this one
        # field rather than guessing -- see ADR-0010.
        return TypeRef(kind="primitive", cpp_name="nlohmann::json")

    def _convert_object_properties(
        self, properties: dict, required: list[str], from_file: str, name: str
    ) -> list[FieldIR]:
        fields: list[FieldIR] = []
        for prop_name, prop_schema in properties.items():
            type_ref = self._property_type_ref(prop_schema, from_file, name)
            field_name = _cpp_field_name(prop_name)
            # A field whose C++ name is identical to its own type's C++ name
            # (observed in the R19 YAML, e.g. field "Snssai" of type Snssai)
            # is legal in isolation but changes name lookup for the rest of
            # the enclosing struct ([-Wchanges-meaning], and a real bug, not
            # just a warning, once other members reference that type). Append
            # '_' to disambiguate -- see ADR-0010.
            if field_name == _bare_type_name(type_ref):
                field_name += "_"
            fields.append(
                FieldIR(
                    json_name=prop_name,
                    cpp_name=field_name,
                    type_ref=type_ref,
                    required=prop_name in required,
                    nullable=bool(prop_schema.get("nullable", False)),
                    description=str(prop_schema.get("description", "")).strip(),
                )
            )
        return fields

    def _convert_one(self, name: str, schema: dict, source_file: str) -> IRType:
        description = str(schema.get("description", "")).strip()
        # From here on, `name` is the sanitized C++ identifier (see
        # cpp_type_name); the caller still keys self.result by the original,
        # unsanitized YAML name, so lookups by $ref name are unaffected.
        name = cpp_type_name(name)

        open_enum_values = _is_open_enum_anyof(schema)
        if open_enum_values is not None and all(isinstance(v, str) for v in open_enum_values):
            return OpenEnumType(
                name=name, source_file=source_file, known_values=open_enum_values, description=description
            )

        if "enum" in schema and all(isinstance(v, str) for v in schema["enum"]):
            return OpenEnumType(
                name=name,
                source_file=source_file,
                known_values=list(schema["enum"]),
                description=description,
            )
        if "enum" in schema:
            # Non-string enum (e.g. boolean/int enum) -- not the 3GPP open-enum
            # pattern this generator targets; fall back rather than guess.
            return OpaqueType(
                name=name,
                source_file=source_file,
                reason=f"non-string enum values: {schema['enum']!r}",
                description=description,
            )

        all_of = schema.get("allOf")
        if isinstance(all_of, list) and all_of and all(_is_pattern_only(m) for m in all_of):
            return AliasType(
                name=name,
                source_file=source_file,
                cpp_underlying="std::string",
                patterns=[m["pattern"] for m in all_of],
                description=description,
            )

        if isinstance(all_of, list) and all_of:
            merged_fields: list[FieldIR] = []
            for member in all_of:
                if "$ref" in member:
                    ref_name, ref_schema, ref_file = self.registry.resolve_ref(member["$ref"], source_file)
                    self._enqueue(ref_name, ref_schema, ref_file)
                    props = ref_schema.get("properties", {})
                    req = ref_schema.get("required", [])
                    merged_fields.extend(self._convert_object_properties(props, req, ref_file, name))
                elif member.get("type") == "object":
                    props = member.get("properties", {})
                    req = member.get("required", [])
                    merged_fields.extend(
                        self._convert_object_properties(props, req, source_file, name)
                    )
                else:
                    return OpaqueType(
                        name=name,
                        source_file=source_file,
                        reason=f"allOf member not $ref/object/pattern-only: {list(member.keys())}",
                        description=description,
                    )
            return ObjectType(name=name, source_file=source_file, fields=merged_fields, description=description)

        if schema.get("type") == "object" or "properties" in schema:
            properties = schema.get("properties", {})
            required = schema.get("required", [])
            fields = self._convert_object_properties(properties, required, source_file, name)
            return ObjectType(name=name, source_file=source_file, fields=fields, description=description)

        if schema.get("type") == "array":
            items = schema.get("items", {})
            inner = self._property_type_ref(items, source_file, name)
            inner_cpp = _type_ref_to_cpp(inner)
            return AliasType(
                name=name,
                source_file=source_file,
                cpp_underlying=f"std::vector<{inner_cpp}>",
                description=description,
            )

        ptype = schema.get("type")
        if ptype in _PRIMITIVE_MAP:
            patterns = []
            if "pattern" in schema:
                patterns.append(schema["pattern"])
            return AliasType(
                name=name,
                source_file=source_file,
                cpp_underlying=_PRIMITIVE_MAP[ptype],
                patterns=patterns,
                description=description,
            )

        return OpaqueType(
            name=name,
            source_file=source_file,
            reason=f"unhandled schema shape, keys={list(schema.keys())}",
            description=description,
        )


def _type_ref_to_cpp(ref: TypeRef) -> str:
    if ref.kind == "array":
        return f"std::vector<{_type_ref_to_cpp(ref.array_of)}>"
    return ref.cpp_name
