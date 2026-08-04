"""Renders the IR (ir.py) into C++ headers/sources via Jinja2 templates.

3GPP's OpenAPI files have genuine cross-file circular dependencies (e.g.
TS29571_CommonData.yaml pulls in TS29510_Nnrf_AccessToken.yaml, which
(transitively) needs types back from CommonData -- confirmed by compiling a
first, naive one-header-per-source-file version and hitting "not declared in
this scope" from #pragma once silently no-op'ing a re-entrant #include before
the needed type was defined). So files are first grouped into strongly
connected components (Tarjan's algorithm) and each SCC is emitted as ONE
header/source pair, with the types *within* it topologically sorted by
field-level dependency so no forward declarations are needed. See
docs/DECISIONS.md ADR-0010.
"""

from __future__ import annotations

import pathlib
import re

import jinja2

from .ir import AliasType, ObjectType, OpaqueType, OpenEnumType, TypeRef

_TEMPLATES_DIR = pathlib.Path(__file__).parent.parent / "templates"


def _type_ref_to_cpp(ref: TypeRef) -> str:
    if ref.kind == "array":
        return f"std::vector<{_type_ref_to_cpp(ref.array_of)}>"
    return ref.cpp_name


def _enumconst(value: str) -> str:
    ident = re.sub(r"[^A-Za-z0-9_]", "_", value)
    if ident[:1].isdigit():
        ident = "V" + ident
    return ident


def _cpp_comment(text: str) -> str:
    """Prefixes every line of a (possibly multi-line) description with '// ',
    so multi-paragraph 3GPP YAML prose can never leak un-commented raw text
    (with embedded quotes/decimal-looking clause numbers like '23.003') into
    the generated file as bare code. A first pass of this template emitted
    only the first line commented, which does not compile -- see ADR-0010."""
    lines = text.replace("\r\n", "\n").split("\n")
    return "\n".join(f"// {line}".rstrip() for line in lines)


def ts_number_from_filename(stem: str) -> str:
    """'TS29571_CommonData' -> 'TS 29.571'. Derived from the filename, not
    invented -- 3GPP's own repo naming convention encodes the TS number."""
    m = re.match(r"TS(\d{5})_", stem)
    if not m:
        return stem
    digits = m.group(1)
    return f"TS {digits[:2]}.{digits[2:]}"


class RenderField:
    def __init__(self, f, optional: bool):
        self.json_name = f.json_name
        self.cpp_name = f.cpp_name
        self.optional = optional
        inner = _type_ref_to_cpp(f.type_ref)
        self.cpp_type = f"std::optional<{inner}>" if optional else inner


def _referenced_names(t) -> list[str]:
    """Named types directly referenced by t (for dependency graphs, both
    file-level and type-level)."""
    names: list[str] = []

    def walk(ref: TypeRef) -> None:
        if ref.kind == "array":
            walk(ref.array_of)
        elif ref.kind == "ref":
            names.append(ref.cpp_name)

    if isinstance(t, ObjectType):
        for f in t.fields:
            walk(f.type_ref)
    return names


def _tarjan_scc(nodes: list[str], edges: dict[str, set[str]]) -> list[list[str]]:
    """Standard Tarjan's algorithm, iterative order preserved via index/lowlink."""
    index_counter = [0]
    stack: list[str] = []
    on_stack: set[str] = set()
    indices: dict[str, int] = {}
    lowlink: dict[str, int] = {}
    result: list[list[str]] = []

    def strongconnect(v: str) -> None:
        indices[v] = index_counter[0]
        lowlink[v] = index_counter[0]
        index_counter[0] += 1
        stack.append(v)
        on_stack.add(v)

        for w in edges.get(v, ()):
            if w not in indices:
                strongconnect(w)
                lowlink[v] = min(lowlink[v], lowlink[w])
            elif w in on_stack:
                lowlink[v] = min(lowlink[v], indices[w])

        if lowlink[v] == indices[v]:
            component = []
            while True:
                w = stack.pop()
                on_stack.discard(w)
                component.append(w)
                if w == v:
                    break
            result.append(component)

    import sys

    old_limit = sys.getrecursionlimit()
    sys.setrecursionlimit(max(old_limit, 10000))
    try:
        for v in nodes:
            if v not in indices:
                strongconnect(v)
    finally:
        sys.setrecursionlimit(old_limit)

    return result


def _topo_sort_types(names: list[str], types_by_name: dict, all_names_in_group: set[str]) -> list[str]:
    """Topologically sorts types within a group by field-level dependency (only
    counting deps on other types in the same group -- cross-group deps are
    #includes, already satisfied). Falls back to input order for any residual
    cycle (rare -- none observed in the R19 pilot files; flagged rather than
    silently assumed impossible)."""
    edges = {n: set() for n in names}
    for n in names:
        for dep in _referenced_names(types_by_name[n]):
            if dep in all_names_in_group and dep != n:
                edges[n].add(dep)

    visited: set[str] = set()
    temp_mark: set[str] = set()
    order: list[str] = []
    had_cycle = False

    def visit(n: str) -> None:
        nonlocal had_cycle
        if n in visited:
            return
        if n in temp_mark:
            had_cycle = True
            return
        temp_mark.add(n)
        for dep in edges[n]:
            visit(dep)
        temp_mark.discard(n)
        visited.add(n)
        order.append(n)

    for n in names:
        visit(n)

    if had_cycle:
        return names
    return order


def render(ir_types: dict, commit: str, out_dir: pathlib.Path) -> list[pathlib.Path]:
    env = jinja2.Environment(
        loader=jinja2.FileSystemLoader(str(_TEMPLATES_DIR)),
        trim_blocks=True,
        lstrip_blocks=True,
    )
    env.filters["enumconst"] = _enumconst
    env.filters["cppcomment"] = _cpp_comment

    name_to_file = {t.name: t.source_file for t in ir_types.values()}
    name_to_type = {t.name: t for t in ir_types.values()}

    by_file: dict[str, list[str]] = {}
    for t in ir_types.values():
        by_file.setdefault(t.source_file, []).append(t.name)

    file_stems = {f: pathlib.Path(f).stem for f in by_file}

    # File-level dependency graph (for SCC / cycle detection).
    file_edges: dict[str, set[str]] = {stem: set() for stem in file_stems.values()}
    for source_file, names in by_file.items():
        self_stem = file_stems[source_file]
        for n in names:
            for dep_name in _referenced_names(name_to_type[n]):
                dep_file = name_to_file.get(dep_name)
                if dep_file is not None:
                    dep_stem = file_stems[dep_file]
                    if dep_stem != self_stem:
                        file_edges[self_stem].add(dep_stem)

    sccs = _tarjan_scc(list(file_stems.values()), file_edges)

    stem_to_group_id: dict[str, int] = {}
    groups: list[dict] = []
    for i, scc in enumerate(sccs):
        for stem in scc:
            stem_to_group_id[stem] = i
        groups.append({"stems": scc, "types": [], "source_files": []})

    stem_to_source_file = {v: k for k, v in file_stems.items()}
    for source_file, names in by_file.items():
        gid = stem_to_group_id[file_stems[source_file]]
        groups[gid]["types"].extend(names)
        groups[gid]["source_files"].append(source_file)

    written: list[pathlib.Path] = []
    out_dir.mkdir(parents=True, exist_ok=True)

    group_name_for_stem: dict[str, str] = {}
    for group in groups:
        stems_sorted = sorted(group["stems"])
        group_name = stems_sorted[0] if len(stems_sorted) == 1 else stems_sorted[0] + "_grp"
        group["name"] = group_name
        for stem in group["stems"]:
            group_name_for_stem[stem] = group_name

    for group in groups:
        group_name = group["name"]
        all_names_in_group = set(group["types"])
        ordered_names = _topo_sort_types(group["types"], name_to_type, all_names_in_group)

        deps: set[str] = set()
        object_types = []
        open_enum_types = []
        alias_types = []
        opaque_types = []

        for n in ordered_names:
            t = name_to_type[n]
            if isinstance(t, ObjectType):
                render_fields = []
                for f in t.fields:
                    optional = (not f.required) or f.nullable
                    render_fields.append(RenderField(f, optional))
                object_types.append((t, render_fields))
                for dep_name in _referenced_names(t):
                    dep_file = name_to_file.get(dep_name)
                    if dep_file is not None:
                        dep_stem = file_stems[dep_file]
                        dep_group = group_name_for_stem[dep_stem]
                        if dep_group != group_name:
                            deps.add(dep_group)
            elif isinstance(t, OpenEnumType):
                open_enum_types.append(t)
            elif isinstance(t, AliasType):
                alias_types.append(t)
            elif isinstance(t, OpaqueType):
                opaque_types.append(t)

        citations = [
            (ts_number_from_filename(pathlib.Path(sf).stem), sf) for sf in sorted(group["source_files"])
        ]

        ctx = {
            "stem": group_name,
            "citations": citations,
            "commit": commit,
            "deps": sorted(deps),
            "object_types": object_types,
            "open_enum_types": open_enum_types,
            "alias_types": alias_types,
            "opaque_types": opaque_types,
        }

        hpp = env.get_template("header.hpp.j2").render(**ctx)
        cpp = env.get_template("source.cpp.j2").render(**ctx)

        hpp_path = out_dir / f"{group_name}.hpp"
        cpp_path = out_dir / f"{group_name}.cpp"
        hpp_path.write_text(hpp, encoding="utf-8")
        cpp_path.write_text(cpp, encoding="utf-8")
        written.extend([hpp_path, cpp_path])

    return written
