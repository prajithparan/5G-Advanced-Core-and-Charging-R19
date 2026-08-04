"""Intermediate representation for 3GPP OpenAPI schemas, on the way to C++.

Kept deliberately small: only the shapes actually observed in the R19 YAML are
modeled (see docs/DECISIONS.md ADR-0010 for the evaluation that led here).
Anything not confidently handled becomes an OpaqueType (nlohmann::json
passthrough) rather than silently-wrong generated code.
"""

from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class TypeRef:
    """A reference to a type used in a field/array-item position."""

    # One of: "ref" (named type, cpp_name holds the name), "primitive"
    kind: str
    cpp_name: str
    # For arrays: the element TypeRef. None otherwise.
    array_of: "TypeRef | None" = None


@dataclass
class FieldIR:
    json_name: str
    cpp_name: str
    type_ref: TypeRef
    required: bool
    nullable: bool
    description: str = ""


@dataclass
class ObjectType:
    name: str
    source_file: str
    fields: list[FieldIR] = field(default_factory=list)
    description: str = ""


@dataclass
class OpenEnumType:
    """anyOf: [{enum: [...]}, {type: string}] -- 3GPP's open/extensible enum
    pattern. Represented as a plain string with named constants for the known
    values, so any value (known or not) round-trips -- see ADR-0010."""

    name: str
    source_file: str
    known_values: list[str] = field(default_factory=list)
    description: str = ""


@dataclass
class AliasType:
    """A scalar (string/integer/number/boolean) type alias, optionally with
    regex pattern constraint(s) collected from `pattern` and/or `allOf`-combined
    patterns."""

    name: str
    source_file: str
    cpp_underlying: str
    patterns: list[str] = field(default_factory=list)
    description: str = ""


@dataclass
class OpaqueType:
    """Fallback for schema shapes not confidently handled: passthrough as
    nlohmann::json rather than emitting a guess."""

    name: str
    source_file: str
    reason: str
    description: str = ""


IRType = ObjectType | OpenEnumType | AliasType | OpaqueType
