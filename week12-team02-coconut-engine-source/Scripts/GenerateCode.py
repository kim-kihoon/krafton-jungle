"""
GenerateCode.py — Unreal-style header tool for KraftonEngine.

Scans Source/ for headers containing UCLASS() and emits per-class:
    Intermediate/Generated/Inc/<File>.generated.h
    Intermediate/Generated/Source/<File>.gen.cpp

Usage:
    python Scripts/GenerateCode.py [--clean] [--verbose]
"""

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path


# ──────────────────────────────────────────────
# Paths
# ──────────────────────────────────────────────
ROOT = Path(__file__).resolve().parent.parent
SOURCE_ROOTS = [
    ROOT / "KraftonEngine" / "Source" / "Engine",
    ROOT / "KraftonEngine" / "Source" / "Editor",
    ROOT / "KraftonEngine" / "Source" / "ObjViewer",
    ROOT / "KraftonEngine" / "Source" / "Game",
]
OUT_INC = ROOT / "KraftonEngine" / "Intermediate" / "Generated" / "Inc"
OUT_SRC = ROOT / "KraftonEngine" / "Intermediate" / "Generated" / "Source"

# Match include-root order from CLAUDE.md so generated #include paths
# resolve under the same roots existing code uses.
INCLUDE_ROOTS = [
    ROOT / "KraftonEngine" / "Source" / "Engine",
    ROOT / "KraftonEngine" / "Source",
    ROOT / "KraftonEngine" / "Source" / "Editor",
    ROOT / "KraftonEngine" / "Source" / "ObjViewer",
]


class CodegenError(Exception):
    pass


# ──────────────────────────────────────────────
# Source Discovery
# ──────────────────────────────────────────────
def discover_headers() -> list[Path]:
    headers = []
    for root in SOURCE_ROOTS:
        if not root.exists():
            continue
        for path in root.rglob("*.h"):
            text = path.read_text(encoding="utf-8-sig")  # tolerate BOM
            if any(marker in text for marker in ("UCLASS(", "UENUM(", "USTRUCT(")):
                headers.append(path)
    return headers


# ──────────────────────────────────────────────
# Comment Stripping
# ──────────────────────────────────────────────
_BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
_LINE_COMMENT  = re.compile(r"//[^\n]*")

def strip_comments(src: str) -> str:
    src = _BLOCK_COMMENT.sub("", src)
    src = _LINE_COMMENT.sub("", src)
    return src


# ──────────────────────────────────────────────
# Parse Model
# ──────────────────────────────────────────────
@dataclass
class PropertyInfo:
    name: str                  # member name (C++ identifier)
    cpp_type: str              # raw C++ type as written
    prop_type: str             # EPropertyType::Float etc
    category: str = ""
    display_name: str | None = None  # DisplayName="..." — falls back to name
    flags: list[str] = field(default_factory=lambda: ["CPF_None"])
    min: str | None = None     # raw expression, emitted as-is
    max: str | None = None
    speed: str | None = None
    enum_type: str | None = None
    enum_expr: str | None = None
    struct_type: str | None = None
    array_inner_type: str | None = None  # for TArray<T>
    property_class: str | None = None    # for FObjectPropertyBase derivatives


@dataclass
class FunctionInfo:
    name: str
    flags: list[str] = field(default_factory=list)
    lua_name: str | None = None   # LuaName="..." override

    @property
    def is_lua(self) -> bool:
        return "Lua" in self.flags


@dataclass
class ClassInfo:
    name: str                # ex: APlayerController
    parent: str              # ex: APawn
    class_flags: list[str] = field(default_factory=lambda: ["CF_None"])
    properties: list[PropertyInfo] = field(default_factory=list)
    functions: list[FunctionInfo] = field(default_factory=list)
    hidden_properties: list[str] = field(default_factory=list)  # UPROPERTY_HIDE("Name")
    no_factory: bool = False  # UCLASS(NoFactory) — skip FObjectFactory registration

@dataclass
class EnumInfo:
    name: str
    entries: list[str]
    underlying_type: str | None = None
    cpp_form: str = "EnumClass"

@dataclass
class StructInfo:
    name: str
    properties: list[PropertyInfo] = field(default_factory=list)

# @dataclass
# class HeaderInfo:
#     classes: list[ClassInfo] = field(default_factory=list)
#     enums: list[EnumInfo] = field(default_factory=list)


# ──────────────────────────────────────────────
# Regex Patterns
# ──────────────────────────────────────────────
# Annotation argument lists may contain:
#   - quoted strings with ')' chars: DisplayName="Amplitude (deg)"
#   - C++ casts and function calls: Enum=StaticEnum_EFoo(), Class=UFoo::StaticClass()
#   - multi-line layouts with newlines for readability
# Three alternatives in the inner group: non-special char, atomic quoted
# string, atomic single-level paren group. The paren-group branch handles
# casts/sizeof without confusing the outer ) that closes the annotation
# itself. Nested parens (e.g. sizeof(decltype(...))) are NOT supported in v1.
ANNOTATION_ARGS_RE = r'((?:[^)"(]|"[^"]*"|\([^)]*\))*)'

# UCLASS(...) followed by class declaration. Captures: flags, class name, parent.
CLASS_RE = re.compile(
    rf"UCLASS\s*\({ANNOTATION_ARGS_RE}\)\s*"
    r"class\s+(?:\w+\s+)?(\w+)\s*"          # optional API-export tag
    r":\s*public\s+(\w+)",                  # single public base
    re.MULTILINE,
)

ENUM_RE = re.compile(
    rf"UENUM\s*\({ANNOTATION_ARGS_RE}\)\s*"
    r"enum\s+(?:(class)\s+)?(\w+)"
    r"(?:\s*:\s*(\w+))?",
    re.MULTILINE,
)

# UPROPERTY(...) <decl-up-to-semicolon> ;
PROPERTY_RE = re.compile(
    rf"UPROPERTY\s*\({ANNOTATION_ARGS_RE}\)\s*([^;]+);",
    re.MULTILINE,
)

# UFUNCTION(...) <return-type> <name> ( ... ) ;
FUNCTION_RE = re.compile(
    rf"UFUNCTION\s*\({ANNOTATION_ARGS_RE}\)\s*"
    r"[\w\s\*&:<>,]+?\s+"                   # return type (greedy-ish)
    r"(\w+)\s*\([^)]*\)\s*[^;{]*[;{]",
    re.MULTILINE,
)


STRUCT_RE = re.compile(
    rf"USTRUCT\s*\({ANNOTATION_ARGS_RE}\)\s*"
    r"struct\s+(?:\w+\s+)?(\w+)",
    re.MULTILINE,
)

# Stand-alone class-body marker — no associated declaration follows.
# UPROPERTY_HIDE("Material") suppresses an inherited property in the editor.
HIDE_PROPERTY_RE = re.compile(r'UPROPERTY_HIDE\s*\(\s*"([^"]+)"\s*\)')


# ──────────────────────────────────────────────
# Attribute Parser
# ──────────────────────────────────────────────
_KV_RE = re.compile(r'(\w+)\s*=\s*("[^"]*"|[^,]+)')

def parse_attributes(attr_text: str) -> tuple[list[str], dict[str, str]]:
    """Returns (flags, key_value_map). Strings keep their quotes stripped."""
    flags: list[str] = []
    kvs: dict[str, str] = {}

    def take_kv(m):
        key, val = m.group(1), m.group(2).strip()
        if val.startswith('"') and val.endswith('"'):
            val = val[1:-1]
        kvs[key] = val
        return ""

    remainder = _KV_RE.sub(take_kv, attr_text)

    for token in remainder.split(","):
        token = token.strip()
        if token:
            flags.append(token)
    return flags, kvs


PROPERTY_FLAG_MAP = {
    "None":                      "CPF_None",
    "Edit":                      "CPF_Edit",
    "Transient":                 "CPF_Transient",
    "DuplicateTransient":        "CPF_DuplicateTransient",
    "NonPIEDuplicateTransient":  "CPF_NonPIEDuplicateTransient",
    "Config":                    "CPF_Config",
    "FixedSize":                 "CPF_FixedSize",
}

CLASS_FLAG_MAP = {
    "Actor":                 "CF_Actor",
    "Component":             "CF_Component",
    "Camera":                "CF_Camera",
    "HiddenInComponentList": "CF_HiddenInComponentList",
}

# Mirrors EClassCastFlags in Engine/Object/UClass.h. Only the "fast-path root"
# class for each bit is listed here — derived classes inherit ancestor bits at
# runtime via the UClass ctor's `InCastFlags | parent->ClassCastFlags` step, so
# emitting the flag once on the root is enough.
#
# To add a new fast-path class: add a CASTCLASS_* bit in UClass.h and an entry
# here keyed on the C++ class name.
CAST_FLAG_MAP = {
    "UField":                 "CASTCLASS_UField",
    "UEnum":                  "CASTCLASS_UEnum",
    "UStruct":                "CASTCLASS_UStruct",
    "UScriptStruct":          "CASTCLASS_UScriptStruct",
    "UClass":                 "CASTCLASS_UClass",
    "UFunction":              "CASTCLASS_UFunction",
    "AActor":                 "CASTCLASS_AActor",
    "APawn":                  "CASTCLASS_APawn",
    "APlayerController":      "CASTCLASS_APlayerController",
    "USceneComponent":        "CASTCLASS_USceneComponent",
    "UPrimitiveComponent":    "CASTCLASS_UPrimitiveComponent",
    "UStaticMeshComponent":   "CASTCLASS_UStaticMeshComponent",
    "USkinnedMeshComponent":  "CASTCLASS_USkinnedMeshComponent",
    "USkeletalMeshComponent": "CASTCLASS_USkeletalMeshComponent",
}

# UCLASS attributes consumed by codegen itself (not forwarded to CF_*).
CLASS_META_ATTRS = {"NoFactory"}


# ──────────────────────────────────────────────
# Type Mapping
# ──────────────────────────────────────────────
TYPE_MAP = {
    "bool":            ("EPropertyType::Bool",     "PROPERTY_BOOL"),
    "int":             ("EPropertyType::Int",      "PROPERTY_INT"),
    "int32":           ("EPropertyType::Int",      "PROPERTY_INT"),
    "uint32":          ("EPropertyType::Int",      "PROPERTY_INT"),
    "float":           ("EPropertyType::Float",    "PROPERTY_FLOAT"),
    "FVector":         ("EPropertyType::Vec3",     "PROPERTY_VEC3"),
    "FVector4":        ("EPropertyType::Vec4",     None),
    "FRotator":        ("EPropertyType::Rotator",  None),
    "FString":         ("EPropertyType::String",   "PROPERTY_STRING"),
    "std::string":     ("EPropertyType::String",   "PROPERTY_STRING"),
    "FSoftObjectPath": ("EPropertyType::SoftObject", None),
    "FName":           ("EPropertyType::Name",     None),
    "FMaterialSlot":   ("EPropertyType::MaterialSlot", None),
}

# uint8 → ByteBool requires explicit Type=ByteBool override since uint8 is
# ambiguous with small-int usage. Default to Int if no override.
TYPE_MAP["uint8"] = ("EPropertyType::Int", "PROPERTY_INT")

POINTER_TYPE_MAP = {
    "USceneComponent": "EPropertyType::SceneComponentRef",
}


def classify_type(
    cpp_type: str,
    known_enums: dict[str, EnumInfo],
    known_structs: dict[str, StructInfo],
) -> tuple[str, str | None, str | None]:
    """Returns (EPropertyType, helper_macro_or_None, array_inner_type_or_None)."""
    t = cpp_type.strip()

    if t.endswith("*"):
        base = t.rstrip("*").strip()
        et = POINTER_TYPE_MAP.get(base)
        if et:
            return et, None, None
        raise CodegenError(
            f"unknown pointer type '{t}' — add to POINTER_TYPE_MAP or use UPROPERTY(Type=...)"
        )

    if t.startswith("TObjectPtr<") and t.endswith(">"):
        inner = t[len("TObjectPtr<"):-1].strip()
        # Inner type is carried in the third slot; parse_property routes it to
        # PropertyInfo.property_class so the FObjectProperty constructor gets
        # `InnerType::StaticClass()` as its PropertyClass argument.
        return "EPropertyType::Object", None, inner

    if t.startswith("TArray<") and t.endswith(">"):
        inner = t[len("TArray<"):-1].strip()
        return "EPropertyType::Array", None, inner

    if t in TYPE_MAP:
        et, helper = TYPE_MAP[t]
        return et, helper, None
    
    if t in known_enums:
        return "EPropertyType::Enum", None, None
    
    if t in known_structs:
        return "EPropertyType::Struct", None, None

    raise CodegenError(
        f"unknown type '{t}' — add to TYPE_MAP, mark as UENUM/USTRUCT, or use UPROPERTY(Type=...)"
    )


# ──────────────────────────────────────────────
# Header Parser
# ──────────────────────────────────────────────
def find_braced_body(src: str, start_idx: int) -> str:
    """Given an offset just after a declaration, return text between
    the matching {...}. Handles nested braces (inner struct/method bodies)."""
    i = src.find("{", start_idx)
    if i == -1:
        return ""
    depth = 1
    body_start = i + 1
    i += 1
    while i < len(src) and depth > 0:
        ch = src[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return src[body_start:i]
        i += 1
    return src[body_start:]   # unterminated — return what we have


def parse_enum_entries(body: str) -> list[str]:
    entries: list[str] = []
    for raw_entry in body.split(","):
        entry = raw_entry.split("=", 1)[0].strip()
        if not entry:
            continue
        if not re.fullmatch(r"\w+", entry):
            raise CodegenError(f"cannot parse enum entry: {raw_entry.strip()!r}")
        entries.append(entry)
    return entries


def parse_enums(path: Path) -> list[EnumInfo]:
    text = strip_comments(path.read_text(encoding="utf-8-sig"))
    enums: list[EnumInfo] = []

    for m in ENUM_RE.finditer(text):
        _, class_keyword, name, underlying_type = m.group(1), m.group(2), m.group(3), m.group(4)
        body = find_braced_body(text, m.end())
        if not body:
            raise CodegenError(f"{path}: could not locate enum body for {name}")
        entries = parse_enum_entries(body)
        if not entries:
            raise CodegenError(f"{path}: enum {name} has no entries")
        cpp_form = "EnumClass" if class_keyword else "Regular"
        enums.append(EnumInfo(name=name, entries=entries, underlying_type=underlying_type, cpp_form=cpp_form))
    return enums


def build_enum_registry(headers: list[Path]) -> dict[str, EnumInfo]:
    registry: dict[str, EnumInfo] = {}
    owners: dict[str, Path] = {}
    for path in headers:
        for enum in parse_enums(path):
            if enum.name in registry:
                raise CodegenError(
                    f"duplicate UENUM {enum.name}: {path} and {owners[enum.name]}"
                )
            registry[enum.name] = enum
            owners[enum.name] = path
    return registry


def parse_structs(
    path: Path,
    known_enums: dict[str, EnumInfo],
    known_structs: dict[str, StructInfo],
) -> list[StructInfo]:
    text = strip_comments(path.read_text(encoding="utf-8-sig"))
    structs: list[StructInfo] = []

    for m in STRUCT_RE.finditer(text):
        _, name = m.group(1), m.group(2)
        body = find_braced_body(text, m.end())
        if not body:
            raise CodegenError(f"{path}: could not locate struct body for {name}")
        properties = [
            parse_property(pm.group(1), pm.group(2), known_enums, known_structs)
            for pm in PROPERTY_RE.finditer(body)
        ]
        structs.append(StructInfo(name=name, properties=properties))
    return structs


def build_struct_registry(
    headers: list[Path],
    known_enums: dict[str, EnumInfo],
) -> dict[str, StructInfo]:
    registry: dict[str, StructInfo] = {}
    owners: dict[str, Path] = {}

    # Seed the registry with declarations so properties in one generated struct
    # can resolve a generated struct type declared later in the scan.
    for path in headers:
        text = strip_comments(path.read_text(encoding="utf-8-sig"))
        for m in STRUCT_RE.finditer(text):
            _, name = m.group(1), m.group(2)
            if name in registry:
                raise CodegenError(
                    f"duplicate USTRUCT {name}: {path} and {owners[name]}"
                )
            registry[name] = StructInfo(name=name)
            owners[name] = path

    for path in headers:
        for struct in parse_structs(path, known_enums, registry):
            registry[struct.name] = struct
    return registry


def parse_property(
    attr_text: str,
    decl_text: str,
    known_enums: dict[str, EnumInfo],
    known_structs: dict[str, StructInfo],
) -> PropertyInfo:
    flags_raw, kvs = parse_attributes(attr_text)

    # Strip default initializer ("= 50.f")
    if "=" in decl_text:
        decl_text = decl_text.split("=", 1)[0]
    decl_text = decl_text.strip()

    # Last identifier = member name, everything before = type.
    m = re.match(r"(.+?)\s+(\w+)\s*$", decl_text)
    if not m:
        raise CodegenError(f"cannot parse property declaration: {decl_text!r}")
    cpp_type, name = m.group(1).strip(), m.group(2)

    # Strip leading CV/storage qualifiers — they don't affect property type
    # classification. Only the head is stripped so inner template args like
    # TArray<const T*> keep their qualifiers intact.
    cpp_type = re.sub(r"^\s*((?:mutable|const|volatile)\s+)+", "", cpp_type).strip()
    enum_type = None
    enum_expr = None
    struct_type = None
    inferred_property_class = None   # only the else-branch (auto-classify) infers from TObjectPtr<T>

    # Explicit Type= override bypasses classify_type.
    if "Type" in kvs:
        prop_type = f"EPropertyType::{kvs['Type']}"
        array_inner = None
        if prop_type == "EPropertyType::Enum":
            enum_expr = kvs.get("Enum")
            if not enum_expr and cpp_type in known_enums:
                enum_type = cpp_type
        if prop_type == "EPropertyType::Struct":
            struct_type = kvs.get("Struct", cpp_type)
    else:
        prop_type, _, inner = classify_type(cpp_type, known_enums, known_structs)
        # The third slot from classify_type is multipurpose: array element type
        # for TArray, pointee class for TObjectPtr. Route it to the right slot
        # based on prop_type.
        array_inner = inner if prop_type == "EPropertyType::Array" else None
        inferred_property_class = inner if prop_type == "EPropertyType::Object" else None
        if cpp_type in known_enums:
            enum_type = cpp_type
        if cpp_type in known_structs:
            struct_type = cpp_type

    flags = [PROPERTY_FLAG_MAP.get(f, f"CPF_{f}") for f in flags_raw] or ["CPF_None"]

    return PropertyInfo(
        name=name,
        cpp_type=cpp_type,
        prop_type=prop_type,
        category=kvs.get("Category", ""),
        display_name=kvs.get("DisplayName"),
        flags=flags,
        min=kvs.get("Min"),
        max=kvs.get("Max"),
        speed=kvs.get("Speed"),
        enum_type=enum_type,
        enum_expr=enum_expr,
        struct_type=struct_type,
        array_inner_type=array_inner,
        property_class=kvs.get("Class") or inferred_property_class,
    )


def parse_function(attr_text: str, name: str) -> FunctionInfo:
    flags_raw, kvs = parse_attributes(attr_text)
    return FunctionInfo(name=name, flags=flags_raw, lua_name=kvs.get("LuaName"))


def parse_header(
    path: Path,
    known_enums: dict[str, EnumInfo],
    known_structs: dict[str, StructInfo],
) -> list[ClassInfo]:
    text = strip_comments(path.read_text(encoding="utf-8-sig"))
    classes: list[ClassInfo] = []

    for m in CLASS_RE.finditer(text):
        attr_text, name, parent = m.group(1), m.group(2), m.group(3)
        flags_raw, _ = parse_attributes(attr_text)

        # Split codegen-meta attrs (NoFactory) from runtime class flags (CF_*).
        meta = {f for f in flags_raw if f in CLASS_META_ATTRS}
        runtime_flags_raw = [f for f in flags_raw if f not in CLASS_META_ATTRS]
        class_flags = (
            [CLASS_FLAG_MAP.get(f, f"CF_{f}") for f in runtime_flags_raw] or ["CF_None"]
        )

        body = find_braced_body(text, m.end())
        if not body:
            raise CodegenError(f"{path}: could not locate class body for {name}")

        properties = [
            parse_property(pm.group(1), pm.group(2), known_enums, known_structs)
            for pm in PROPERTY_RE.finditer(body)
        ]
        functions = [
            parse_function(fm.group(1), fm.group(2))
            for fm in FUNCTION_RE.finditer(body)
        ]
        hidden_properties = [
            hm.group(1) for hm in HIDE_PROPERTY_RE.finditer(body)
        ]

        classes.append(ClassInfo(
            name=name,
            parent=parent,
            class_flags=class_flags,
            properties=properties,
            functions=functions,
            hidden_properties=hidden_properties,
            no_factory="NoFactory" in meta,
        ))
    return classes


# ──────────────────────────────────────────────
# Emission — .generated.h
# ──────────────────────────────────────────────
GENERATED_H_TEMPLATE = """\
// AUTOGENERATED by GenerateCode.py — do not edit.
#pragma once

{body_macros}
"""

CLASS_MACRO_TEMPLATE = """\
#define KE_GENERATED_BODY_{class_name}() \\
    using Super = {parent}; \\
    static UClass StaticClassInstance; \\
    static UClass* StaticClass() {{ return &StaticClassInstance; }} \\
    UClass* GetClass() const override {{ return StaticClass(); }} \\
    friend struct {class_name}_PropertyRegistrar;
"""

STRUCT_MACRO_TEMPLATE = """\
#define KE_GENERATED_BODY_{struct_name}() \\
    static class UScriptStruct StaticStructInstance; \\
    static class UScriptStruct* StaticStruct() {{ return &StaticStructInstance; }}
"""

ENUM_FORWARD_TEMPLATE = """\
class UEnum;
UEnum* {static_enum_symbol}();
"""

def emit_generated_header(
    classes: list[ClassInfo],
    enums: list[EnumInfo],
    structs: list[StructInfo],
) -> str:
    sections: list[str] = []
    if classes:
        sections.append("\n".join(
            CLASS_MACRO_TEMPLATE.format(class_name=c.name, parent=c.parent)
            for c in classes
        ))
    if structs:
        sections.append("\n".join(
            STRUCT_MACRO_TEMPLATE.format(struct_name=s.name)
            for s in structs
        ))
    if enums:
        sections.append("\n".join(
            ENUM_FORWARD_TEMPLATE.format(static_enum_symbol=static_enum_symbol(e.name))
            for e in enums
        ))
    return GENERATED_H_TEMPLATE.format(body_macros="\n".join(sections))


# ──────────────────────────────────────────────
# Emission — .gen.cpp
# ──────────────────────────────────────────────
def emit_gen_cpp(
    source_header_include: str,
    classes: list[ClassInfo],
    enums: list[EnumInfo],
    structs: list[StructInfo],
    known_enums: dict[str, EnumInfo],
    known_structs: dict[str, StructInfo],
) -> str:
    has_lua = any(fn.is_lua for c in classes for fn in c.functions)
    out = [
        "// AUTOGENERATED by GenerateCode.py — do not edit.",
        f'#include "{source_header_include}"',
        '#include "Object/ObjectFactory.h"',
    ]
    if enums:
        out.append('#include "Object/UEnum.h"')
    if structs:
        out.append('#include "Object/ScriptStruct.h"')
    out.extend(
        f'#include "{header}"'
        for header in collect_property_headers(classes, structs, known_enums, known_structs)
    )
    if has_lua:
        out.append('#include "Object/LuaClassRegistry.h"')
        out.append("#include <sol/sol.hpp>")
    out.append("")
    for e in enums:
        out.append(emit_enum_schema(e))
    for s in structs:
        out.append(emit_struct_schema(s, known_enums, known_structs))
    for c in classes:
        out.append(emit_class_static(c))
        out.append(emit_property_registrar(c, known_enums, known_structs))
        lua_block = emit_lua_registrar(c)
        if lua_block:
            out.append(lua_block)
    return "\n".join(out)


def static_enum_symbol(enum_name: str) -> str:
    return f"StaticEnum_{enum_name}"


def emit_enum_schema(enum: EnumInfo) -> str:
    cpp_form = "ECppForm::EnumClass" if enum.cpp_form == "EnumClass" else "ECppForm::Regular"
    lines = [
        f"UEnum* {static_enum_symbol(enum.name)}()",
        "{",
        f'    static UEnum Enum("{enum.name}", sizeof({enum.name}), {cpp_form});',
        "    static const bool bRegistered = []()",
        "    {",
    ]
    for entry in enum.entries:
        value_expr = f"{enum.name}::{entry}" if enum.cpp_form == "EnumClass" else entry
        lines.append(
            f'        Enum.AddEnumerator("{entry}", static_cast<int64>({value_expr}));'
        )
    lines.extend([
        "        return true;",
        "    }();",
        "    (void)bRegistered;",
        "    return &Enum;",
        "}",
    ])
    return "\n".join(lines) + "\n"


def emit_struct_schema(
    s: StructInfo,
    known_enums: dict[str, EnumInfo],
    known_structs: dict[str, StructInfo],
) -> str:
    lines = [
        f"static const TCppStructOps<{s.name}> G{s.name}CppStructOps;",
        f"UScriptStruct {s.name}::StaticStructInstance(",
        f'    "{s.name}", nullptr,',
        f"    sizeof({s.name}), alignof({s.name}), &G{s.name}CppStructOps);",
        "",
        f"static void Register{s.name}StructProperties(UScriptStruct* Struct)",
        "{",
        "    static bool bRegistered = false;",
        "    if (bRegistered || !Struct) return;",
        "    bRegistered = true;",
    ]
    for p in s.properties:
        lines.extend(
            "    " + line
            for line in emit_property_registration_into(
                p,
                container_expr="Struct",
                owner_type=s.name,
                known_enums=known_enums,
                known_structs=known_structs,
                error_context="struct field",
            ).splitlines()
        )
    lines.extend([
        "}",
        "",
        f"struct {s.name}_StructPropertyRegistrar {{",
        f"    {s.name}_StructPropertyRegistrar() {{",
        f"        Register{s.name}StructProperties({s.name}::StaticStruct());",
        "    }",
        "};",
        f"static {s.name}_StructPropertyRegistrar s_{s.name}_StructPropertyReg;",
    ])
    return "\n".join(lines) + "\n"


def emit_property_constructor_expr(
    p: PropertyInfo,
    *,
    offset_expr: str,
    size_expr: str,
    known_enums: dict[str, EnumInfo],
    known_structs: dict[str, StructInfo],
    error_context: str,
) -> str:
    flags = " | ".join(p.flags)
    disp = p.display_name or p.name
    base_args = [
        f'"{disp}"',
        f'"{p.category}"',
        flags,
        f"static_cast<uint32>({offset_expr})",
        f"static_cast<uint32>({size_expr})",
    ]

    ctor_name = property_ctor_name(p.prop_type)
    extra_args = emit_property_constructor_args(
        p,
        known_enums=known_enums,
        known_structs=known_structs,
        error_context=error_context,
    )
    return f"new {ctor_name}({', '.join(base_args + extra_args)})"


def emit_class_static(c: ClassInfo) -> str:
    flags = " | ".join(c.class_flags)
    # Only emit a cast flag for fast-path roots; descendants inherit ancestor
    # bits at runtime through the UClass ctor, so the gen.cpp stays terse.
    cast_flag = CAST_FLAG_MAP.get(c.name)
    flag_args = f"{flags}, {cast_flag}" if cast_flag else flags
    parts = [
        f"UClass {c.name}::StaticClassInstance(",
        f'    "{c.name}", &{c.parent}::StaticClassInstance,',
        f"    sizeof({c.name}), {flag_args});",
    ]
    # Default: register with FObjectFactory so SceneSaveManager can spawn
    # this type by name (matches IMPLEMENT_CLASS = DEFINE_CLASS + REGISTER_FACTORY
    # used by 88/90 UObject-derived classes in the engine today).
    if not c.no_factory:
        parts.append(f"REGISTER_FACTORY({c.name})")
    return "\n".join(parts) + "\n"


def property_ctor_name(prop_type: str) -> str:
    return {
        "EPropertyType::Bool": "FBoolProperty",
        "EPropertyType::ByteBool": "FByteBoolProperty",
        "EPropertyType::Int": "FIntProperty",
        "EPropertyType::Float": "FFloatProperty",
        "EPropertyType::Vec3": "FVec3Property",
        "EPropertyType::Vec4": "FVec4Property",
        "EPropertyType::Rotator": "FRotatorProperty",
        "EPropertyType::String": "FStringProperty",
        "EPropertyType::Name": "FNameProperty",
        "EPropertyType::SceneComponentRef": "FSceneComponentRefProperty",
        "EPropertyType::Color4": "FColor4Property",
        "EPropertyType::MaterialSlot": "FMaterialSlotProperty",
        "EPropertyType::Enum": "FEnumProperty",
        "EPropertyType::Struct": "FStructProperty",
        "EPropertyType::Script": "FScriptProperty",
        "EPropertyType::Array": "FArrayProperty",
        "EPropertyType::SoftObject": "FSoftObjectProperty",
        "EPropertyType::Object": "FObjectProperty",
    }.get(prop_type) or (_ for _ in ()).throw(CodegenError(f"unknown property type {prop_type}"))


PROPERTY_HEADER_BY_TYPE = {
    "EPropertyType::Bool": "Core/Property/PropertyTypes.h",
    "EPropertyType::ByteBool": "Core/Property/PropertyTypes.h",
    "EPropertyType::Int": "Core/Property/PropertyTypes.h",
    "EPropertyType::Float": "Core/Property/PropertyTypes.h",
    "EPropertyType::Vec3": "Core/Property/PropertyTypes.h",
    "EPropertyType::Vec4": "Core/Property/PropertyTypes.h",
    "EPropertyType::Rotator": "Core/Property/PropertyTypes.h",
    "EPropertyType::String": "Core/Property/PropertyTypes.h",
    "EPropertyType::Name": "Core/Property/PropertyTypes.h",
    "EPropertyType::SceneComponentRef": "Core/Property/PropertyTypes.h",
    "EPropertyType::Color4": "Core/Property/PropertyTypes.h",
    "EPropertyType::MaterialSlot": "Core/Property/PropertyTypes.h",
    "EPropertyType::Enum": "Core/Property/FEnumProperty.h",
    "EPropertyType::Struct": "Core/Property/FStructProperty.h",
    "EPropertyType::Script": "Core/Property/PropertyTypes.h",
    "EPropertyType::Array": "Core/Property/FArrayProperty.h",
    "EPropertyType::SoftObject": "Core/Property/FObjectPropertyBase/FSoftObjectProperty.h",
    "EPropertyType::Object": "Core/Property/FObjectPropertyBase/FObjectProperty.h",
}


def collect_property_headers(
    classes: list[ClassInfo],
    structs: list[StructInfo],
    known_enums: dict[str, EnumInfo],
    known_structs: dict[str, StructInfo],
) -> list[str]:
    prop_types: set[str] = set()

    for p in [prop for c in classes for prop in c.properties] + [prop for s in structs for prop in s.properties]:
        prop_types.add(p.prop_type)
        if p.prop_type == "EPropertyType::Array":
            if not p.array_inner_type:
                raise CodegenError(f"TArray property {p.name} has no inner type")
            inner_et, _, _ = classify_type(p.array_inner_type, known_enums, known_structs)
            prop_types.add(inner_et)

    headers = {PROPERTY_HEADER_BY_TYPE[prop_type] for prop_type in prop_types}
    return sorted(headers)


def emit_property_constructor_args(
    p: PropertyInfo,
    *,
    known_enums: dict[str, EnumInfo],
    known_structs: dict[str, StructInfo],
    error_context: str,
) -> list[str]:
    if p.prop_type in ("EPropertyType::Int", "EPropertyType::Float"):
        return [p.min or "0.0f", p.max or "0.0f", p.speed or "0.1f"]
    if p.prop_type == "EPropertyType::Enum":
        if p.enum_type:
            enum = known_enums.get(p.enum_type)
            if not enum:
                raise CodegenError(f"unknown generated enum type {p.enum_type}")
            return [f"{static_enum_symbol(enum.name)}()"]
        if p.enum_expr:
            return [p.enum_expr]
        raise CodegenError(
            f"enum {error_context} {p.name}: requires generated UENUM or Enum=StaticEnum_*()"
        )
    if p.prop_type == "EPropertyType::Struct":
        if p.struct_type:
            return [f"{p.struct_type}::StaticStruct()"]
        raise CodegenError(
            f"struct {error_context} {p.name}: requires generated USTRUCT or Struct="
        )
    if p.prop_type == "EPropertyType::SoftObject":
        if not p.property_class:
            raise CodegenError(
                f"soft object {error_context} {p.name}: v1 requires Class="
            )
        return [f"{p.property_class}::StaticClass()"]
    if p.prop_type == "EPropertyType::Object":
        # PropertyClass is normally auto-inferred from TObjectPtr<T>, but Class=
        # override still works (e.g. for legacy raw-pointer fields someday).
        if not p.property_class:
            raise CodegenError(
                f"object {error_context} {p.name}: use TObjectPtr<T> or specify Class="
            )
        return [f"{p.property_class}::StaticClass()"]
    return []


def emit_property_registrar(
    c: ClassInfo,
    known_enums: dict[str, EnumInfo],
    known_structs: dict[str, StructInfo],
) -> str:
    # Skip the registrar entirely only when the class has no codegen-driven
    # property work at all — no UPROPERTYs to register and no UPROPERTY_HIDEs
    # to apply. That still leaves room for a rare hand-written registrar when
    # a class genuinely needs one.
    if not c.properties and not c.hidden_properties:
        return ""

    lines = [
        f"struct {c.name}_PropertyRegistrar {{",
        f"    {c.name}_PropertyRegistrar() {{",
        f"        using ThisClass = {c.name};",
        f"        UClass* Cls = {c.name}::StaticClass();",
        f"        (void)Cls;",
    ]
    for h in c.hidden_properties:
        lines.append(f'        Cls->HideInheritedProperty("{h}");')
    for p in c.properties:
        lines.extend("        " + line for line in emit_property_registration(
            p, known_enums, known_structs
        ).splitlines())
    lines.append("    }")
    lines.append("};")
    lines.append(f"static {c.name}_PropertyRegistrar s_{c.name}_PropertyReg;\n")
    return "\n".join(lines)


def emit_property_registration_into(
    p: PropertyInfo,
    *,
    container_expr: str,
    owner_type: str,
    known_enums: dict[str, EnumInfo],
    known_structs: dict[str, StructInfo],
    error_context: str,
) -> str:
    if p.prop_type == "EPropertyType::Array":
        if not p.array_inner_type:
            raise CodegenError(f"TArray property {p.name} has no inner type")
        inner_et, _, _ = classify_type(p.array_inner_type, known_enums, known_structs)
        inner_info = PropertyInfo(
            name="Element",
            cpp_type=p.array_inner_type,
            prop_type=inner_et,
            category=p.category,
            display_name="Element",
            flags=["CPF_None"],
            enum_type=p.array_inner_type if p.array_inner_type in known_enums else None,
            struct_type=p.array_inner_type if p.array_inner_type in known_structs else None,
        )
        expr = emit_property_constructor_expr(
            p,
            offset_expr=f"offsetof({owner_type}, {p.name})",
            size_expr=f"sizeof((({owner_type}*)0)->{p.name})",
            known_enums=known_enums,
            known_structs=known_structs,
            error_context=error_context,
        )
        inner_expr = emit_property_constructor_expr(
            inner_info,
            offset_expr="0",
            size_expr=f"sizeof({p.array_inner_type})",
            known_enums=known_enums,
            known_structs=known_structs,
            error_context=f"{error_context} array inner",
        )
        return "\n".join([
            "{",
            f"    {container_expr}->AddProperty({expr[:-1]},",
            f"        std::unique_ptr<FProperty>({inner_expr}),",
            f"        GetTArrayAccessor<{p.array_inner_type}>()));",
            "}",
        ])

    expr = emit_property_constructor_expr(
        p,
        offset_expr=f"offsetof({owner_type}, {p.name})",
        size_expr=f"sizeof((({owner_type}*)0)->{p.name})",
        known_enums=known_enums,
        known_structs=known_structs,
        error_context=error_context,
    )
    return "\n".join([
        "{",
        f"    {container_expr}->AddProperty({expr});",
        "}",
    ])


def emit_property_registration(
    p: PropertyInfo,
    known_enums: dict[str, EnumInfo],
    known_structs: dict[str, StructInfo],
) -> str:
    return emit_property_registration_into(
        p,
        container_expr="Cls",
        owner_type="ThisClass",
        known_enums=known_enums,
        known_structs=known_structs,
        error_context="property",
    )


_LUA_PREFIX_STRIP = re.compile(r"^[UAF][A-Z]")

def lua_class_name(cpp_name: str) -> str:
    """Strip leading U/A/F prefix to match existing hand-binding convention
    (UActionComponent → ActionComponent, AActor → Actor, FVector → Vector)."""
    return cpp_name[1:] if _LUA_PREFIX_STRIP.match(cpp_name) else cpp_name


def emit_lua_registrar(c: ClassInfo) -> str:
    """Returns "" when the class has no UFUNCTION(Lua) members — bare UFUNCTION()
    is reserved for future consumers (CallInEditor, Exec) and does not bind to Lua."""
    lua_funcs = [fn for fn in c.functions if fn.is_lua]
    if not lua_funcs:
        return ""

    lua_name = lua_class_name(c.name)
    lines = [
        f"static void {c.name}_RegisterLua(sol::state& Lua) {{",
        f'    Lua.new_usertype<{c.name}>("{lua_name}",',
        f"        sol::base_classes, sol::bases<{c.parent}>(),",
    ]
    for i, fn in enumerate(lua_funcs):
        exposed = fn.lua_name or fn.name
        comma = "," if i < len(lua_funcs) - 1 else ""
        lines.append(f'        "{exposed}", &{c.name}::{fn.name}{comma}')
    lines.append("    );")
    lines.append("}")
    lines.append(f"static FLuaClassRegistrar s_{c.name}_LuaReg(&{c.name}_RegisterLua);\n")
    return "\n".join(lines)


# ──────────────────────────────────────────────
# Include Path Resolution
# ──────────────────────────────────────────────
def make_include_path(header: Path) -> str:
    for root in INCLUDE_ROOTS:
        try:
            rel = header.relative_to(root)
            return rel.as_posix()
        except ValueError:
            continue
    raise CodegenError(f"header {header} not under any include root")


# ──────────────────────────────────────────────
# Output
# ──────────────────────────────────────────────
def write_if_different(path: Path, content: str) -> bool:
    new_bytes = content.encode("utf-8")
    if path.exists() and path.read_bytes() == new_bytes:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(new_bytes)
    return True


def remove_stale_generated_files(
    directory: Path,
    pattern: str,
    expected_outputs: set[Path],
    *,
    verbose: bool = False,
) -> int:
    if not directory.exists():
        return 0

    expected = {path.resolve() for path in expected_outputs}
    removed = 0

    for path in directory.glob(pattern):
        if not path.is_file() or path.resolve() in expected:
            continue

        path.unlink()
        removed += 1
        if verbose:
            print(f"  removed {path.name}")

    return removed


def check_collisions(headers: list[Path]) -> None:
    seen: dict[str, Path] = {}
    for h in headers:
        if h.stem in seen:
            raise CodegenError(
                f"output collision: {h} and {seen[h.stem]} both produce "
                f"{h.stem}.generated.h — rename one"
            )
        seen[h.stem] = h


# ──────────────────────────────────────────────
# Entry Point
# ──────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--verbose", "-v", action="store_true")
    args = parser.parse_args()

    removed = 0
    if args.clean:
        removed += remove_stale_generated_files(OUT_INC, "*.generated.h", set(), verbose=args.verbose)
        removed += remove_stale_generated_files(OUT_SRC, "*.gen.cpp", set(), verbose=args.verbose)

    headers = discover_headers()
    check_collisions(headers)
    known_enums = build_enum_registry(headers)
    known_structs = build_struct_registry(headers, known_enums)

    expected_headers: set[Path] = set()
    expected_sources: set[Path] = set()
    written = 0
    for h in headers:
        enums = parse_enums(h)
        structs = parse_structs(h, known_enums, known_structs)
        classes = parse_header(h, known_enums, known_structs)
        if not classes and not enums and not structs:
            continue

        gh_text = emit_generated_header(classes, enums, structs)
        gh_path = OUT_INC / f"{h.stem}.generated.h"
        expected_headers.add(gh_path)
        if write_if_different(gh_path, gh_text):
            written += 1
            if args.verbose:
                print(f"  wrote {h.stem}.generated.h")

        if classes or enums or structs:
            cpp_path = OUT_SRC / f"{h.stem}.gen.cpp"
            expected_sources.add(cpp_path)
            cpp_text = emit_gen_cpp(
                make_include_path(h),
                classes,
                enums,
                structs,
                known_enums,
                known_structs,
            )
            if write_if_different(cpp_path, cpp_text):
                written += 1
                if args.verbose:
                    print(f"  wrote {h.stem}.gen.cpp")

    removed += remove_stale_generated_files(OUT_INC, "*.generated.h", expected_headers, verbose=args.verbose)
    removed += remove_stale_generated_files(OUT_SRC, "*.gen.cpp", expected_sources, verbose=args.verbose)

    print(f"GenerateCode: scanned {len(headers)} headers, wrote {written} files, removed {removed} stale files.")


if __name__ == "__main__":
    try:
        main()
    except CodegenError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
