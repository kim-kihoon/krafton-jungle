from dataclasses import dataclass
import os
from pathlib import Path
import re
import sys


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parents[1]
GENERATED_DIR = PROJECT_DIR / "Generated"
GENERATED_CPP = GENERATED_DIR / "JSEngine.generated.cpp"

PROPERTY_CLASS_MAP = {
    "bool": "FBoolProperty",
    "int32": "FInt32Property",
    "float": "FFloatProperty",
    "FName": "FNameProperty",
    "FString": "FStringProperty",
    "FStaticMeshAssetRef": "FStaticMeshAssetProperty",
    "FSkeletalMeshAssetRef": "FSkeletalMeshAssetProperty",
    "FTextureAssetRef": "FTextureAssetProperty",
    "FMaterialAssetRef": "FMaterialAssetProperty",
    "FAnimationSequenceAssetRef": "FAnimationSequenceAssetProperty",
    "FAnimStateMachineAssetRef": "FAnimStateMachineAssetProperty",
    "FCurveAssetRef": "FCurveAssetProperty",
    "FSceneAssetRef": "FSceneAssetProperty",
    "FSoundAssetRef": "FSoundAssetProperty",
}

ABSTRACT_CLASSES = {
    "UMeshComponent",
    "USkinnedMeshComponent",
}

FLAG_MAP = {
    "Read": "EPropertyFlags::Read",
    "Write": "EPropertyFlags::Write",
    "Transient": "EPropertyFlags::Transient",
    "LuaRead": "EPropertyFlags::LuaRead",
    "LuaWrite": "EPropertyFlags::LuaWrite",
}

FUNCTION_FLAG_MAP = {
    "Native": "EFunctionFlags::Native",
    "EditorCall": "EFunctionFlags::EditorCall",
    "LuaCallable": "EFunctionFlags::LuaCallable",
}

CLASS_PATTERN = re.compile(
    r"UCLASS\s*\(\s*\)\s*\n\s*class\s+(?P<class>\w+)\s*:\s*public\s+(?P<super>\w+)",
    re.MULTILINE,
)

PROPERTY_PATTERN = re.compile(
    r"UPROPERTY\s*\((?P<flags>[^)]*)\)\s*\n\s*"
    r"(?P<type>TArray\s*<\s*[\w:]+(?:\s*\*)?\s*>|[\w:]+(?:\s*\*)?)\s+"
    r"(?P<name>\w+)\s*(?:=[^;]*)?;",
    re.MULTILINE,
)

FUNCTION_PATTERN = re.compile(
    r"UFUNCTION\s*\((?P<flags>[^)]*)\)\s*\n\s*"
    r"(?:virtual\s+)?"
    r"(?P<return_type>[\w:]+(?:\s*\*)?)\s+"
    r"(?P<name>\w+)\s*\(\s*(?P<params>[^)]*)\s*\)\s*"
    r"(?:const\s*)?(?:override\s*)?;",
    re.MULTILINE,
)

GENERATED_BODY_PATTERN = re.compile(
    r"GENERATED_BODY\s*\(\s*(?P<class>\w+)\s*,\s*(?P<super>\w+)\s*\)",
    re.MULTILINE,
)


@dataclass
class ReflectedProperty:
    name: str
    cpp_type: str
    property_class: str
    flags: str
    object_class: str | None = None
    inner_property_class: str | None = None
    inner_class: str | None = None
    inner_cpp_type: str | None = None
    array_ops_type: str | None = None


@dataclass
class ReflectedFunction:
    name: str
    flags: str


@dataclass
class ReflectedClass:
    file_path: Path
    class_name: str
    super_name: str
    class_flags: str
    is_abstract: bool
    needs_type_info_definition: bool
    properties: list[ReflectedProperty]
    functions: list[ReflectedFunction]


class ReflectionGeneratorError(Exception):
    pass


def should_skip(path: Path) -> bool:
    rel_parts = {part.lower() for part in path.relative_to(PROJECT_DIR).parts}
    return (
        "generated" in rel_parts
        or "thirdparty" in rel_parts
        or "build" in rel_parts
        or "bin" in rel_parts
        or "packages" in rel_parts
        or path.name.endswith(".generated.h")
    )


def line_number(content: str, offset: int) -> int:
    return content.count("\n", 0, offset) + 1


def fail(path: Path, content: str, offset: int, message: str) -> None:
    rel_path = path.relative_to(PROJECT_DIR)
    raise ReflectionGeneratorError(
        f"{rel_path}:{line_number(content, offset)}: {message}"
    )


def normalize_cpp_type(cpp_type: str) -> str:
    return re.sub(r"\s+", "", cpp_type.strip())


def reflected_type(cpp_type: str) -> tuple[str | None, str | None]:
    if cpp_type in PROPERTY_CLASS_MAP:
        return PROPERTY_CLASS_MAP[cpp_type], None

    if cpp_type.endswith("*"):
        class_name = cpp_type[:-1]
        if class_name.startswith(("U", "A")):
            return "FObjectProperty", class_name

    return None, None

def parse_array_cpp_type(cpp_type: str) -> str | None:
    match = re.fullmatch(r"TArray<(?P<inner>[\w:]+\*?)>", cpp_type)
    if not match:
        return None
    return match.group("inner")


def reflected_array_type(
    path: Path,
    content: str,
    offset: int,
    cpp_type: str,
) -> tuple[str, str | None, str, str]:
    inner_cpp_type = parse_array_cpp_type(cpp_type)
    if not inner_cpp_type:
        fail(path, content, offset, f"failed to parse TArray property type '{cpp_type}'")

    if inner_cpp_type == "bool":
        fail(path, content, offset, "TArray<bool> is not supported because std::vector<bool> does not expose bool* elements")

    if parse_array_cpp_type(inner_cpp_type):
        fail(path, content, offset, "nested TArray properties are not supported")

    inner_property_class, inner_class = reflected_type(inner_cpp_type)
    if not inner_property_class:
        fail(path, content, offset, f"unsupported TArray inner type '{inner_cpp_type}'")

    return inner_property_class, inner_class, inner_cpp_type, f"GetArrayPropertyOps<{inner_cpp_type}>()"

def skip_ws_and_comments(text: str, offset: int) -> int:
    while offset < len(text):
        # whitespace skip
        match = re.match(r"\s+", text[offset:])
        if match:
            offset += match.end()
            continue

        # // comment skip
        if text.startswith("//", offset):
            newline = text.find("\n", offset)
            if newline == -1:
                return len(text)
            offset = newline + 1
            continue

        # /* */ comment skip
        if text.startswith("/*", offset):
            end = text.find("*/", offset + 2)
            if end == -1:
                return len(text)
            offset = end + 2
            continue

        break

    return offset


def read_next_declaration(text: str, offset: int) -> tuple[str, int, int]:
    decl_start = skip_ws_and_comments(text, offset)

    semicolon = text.find(";", decl_start)
    if semicolon == -1:
        return "", decl_start, decl_start

    decl_end = semicolon + 1
    return text[decl_start:decl_end], decl_start, decl_end


def parse_property_declaration(
    path: Path,
    content: str,
    body_start: int,
    parseable_body: str,
    decl_text: str,
    decl_offset: int,
) -> tuple[str, str]:
    decl_match = PROPERTY_DECL_PATTERN.match(decl_text)
    absolute_offset = body_start + decl_offset

    if not decl_match:
        fail(
            path,
            content,
            absolute_offset,
            f"failed to parse UPROPERTY declaration '{decl_text.strip()}'",
        )

    raw_cpp_type = decl_match.group("type").strip()
    prop_name = decl_match.group("name").strip()

    if ("<" in raw_cpp_type or ">" in raw_cpp_type) and not parse_array_cpp_type(normalize_cpp_type(raw_cpp_type)):
        fail(
            path,
            content,
            absolute_offset,
            f"template UPROPERTY types are not supported: {raw_cpp_type}",
        )

    return raw_cpp_type, prop_name

def reflected_type_for_flags(
    path: Path,
    content: str,
    offset: int,
    cpp_type: str,
    flag_parts: list[str],
) -> tuple[str | None, str | None, str | None, str | None, str | None, str | None]:
    inner_cpp_type = parse_array_cpp_type(cpp_type)
    if inner_cpp_type:
        inner_property_class, inner_class, normalized_inner_cpp_type, array_ops_type = reflected_array_type(
            path,
            content,
            offset,
            cpp_type,
        )
        return (
            "FArrayProperty",
            None,
            inner_property_class,
            inner_class,
            normalized_inner_cpp_type,
            array_ops_type,
        )

    prop_type, object_class = reflected_type(cpp_type)
    return prop_type, object_class, None, None, None, None


def parse_flags(path: Path, content: str, offset: int, flag_text: str) -> str:
    parts = [part.strip() for part in flag_text.split(",") if part.strip()]
    flags: list[str] = []

    if "Edit" in parts:
        flags.extend(
            [
                "EPropertyFlags::Read",
                "EPropertyFlags::Write",
                "EPropertyFlags::Edit",
            ]
        )

    for part in parts:
        if part == "Edit":
            continue
        mapped_flag = FLAG_MAP.get(part)
        if not mapped_flag:
            fail(path, content, offset, f"unsupported UPROPERTY flag '{part}'")
        flags.append(mapped_flag)

    if not flags:
        flags.append("EPropertyFlags::None")

    result: list[str] = []
    for flag in flags:
        if flag not in result:
            result.append(flag)

    return " |\n                                ".join(result)


def parse_function_flags(path: Path, content: str, offset: int, flag_text: str) -> str:
    parts = [part.strip() for part in flag_text.split(",") if part.strip()]
    flags: list[str] = []

    for part in parts:
        mapped_flag = FUNCTION_FLAG_MAP.get(part)
        if not mapped_flag:
            fail(path, content, offset, f"unsupported UFUNCTION flag '{part}'")
        flags.append(mapped_flag)

    if not flags:
        flags.append("EFunctionFlags::None")

    result: list[str] = []
    for flag in flags:
        if flag not in result:
            result.append(flag)

    return " |\n                            ".join(result)


def find_matching_class_body(path: Path, content: str, class_match: re.Match) -> tuple[int, int]:
    open_brace = content.find("{", class_match.end())
    if open_brace == -1:
        fail(path, content, class_match.start(), "reflected class has no body")

    depth = 0
    for index in range(open_brace, len(content)):
        char = content[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                if content[index + 1 : index + 2] != ";":
                    fail(path, content, index, "reflected class body must end with '};'")
                return open_brace + 1, index

    fail(path, content, class_match.start(), "reflected class body is not closed")
    raise AssertionError("unreachable")


def strip_conditional_blocks(body: str) -> str:
    stripped_lines: list[str] = []
    conditional_depth = 0

    for line in body.splitlines(keepends=True):
        directive = line.lstrip()
        if re.match(r"#\s*(if|ifdef|ifndef)\b", directive):
            conditional_depth += 1
            stripped_lines.append("\n")
            continue
        if re.match(r"#\s*endif\b", directive):
            conditional_depth = max(0, conditional_depth - 1)
            stripped_lines.append("\n")
            continue

        if conditional_depth > 0:
            stripped_lines.append("\n")
        else:
            stripped_lines.append(line)

    return "".join(stripped_lines)


def validate_generated_body(
    path: Path,
    content: str,
    body_start: int,
    parseable_body: str,
    class_name: str,
    super_name: str,
) -> None:
    generated_body_matches = list(GENERATED_BODY_PATTERN.finditer(parseable_body))
    if not generated_body_matches:
        fail(
            path,
            content,
            body_start,
            f"missing GENERATED_BODY({class_name}, {super_name}) in reflected class body",
        )
    if len(generated_body_matches) > 1:
        fail(
            path,
            content,
            body_start + generated_body_matches[1].start(),
            "only one GENERATED_BODY(...) is supported per reflected class",
        )

    generated_body_match = generated_body_matches[0]
    body_class_name = generated_body_match.group("class")
    body_super_name = generated_body_match.group("super")

    if body_class_name != class_name or body_super_name != super_name:
        fail(
            path,
            content,
            body_start + generated_body_match.start(),
            f"GENERATED_BODY arguments must match class declaration: "
            f"expected GENERATED_BODY({class_name}, {super_name})",
        )


def infer_class_flags(class_name: str, super_name: str) -> str:
    if class_name.startswith("A") or super_name.startswith("A"):
        return "CF_Actor"
    if class_name.endswith("Component") or super_name.endswith("Component"):
        return "CF_Component"
    return "CF_None"


def is_abstract_class_body(parseable_body: str) -> bool:
    return re.search(r"=\s*0\s*;", parseable_body) is not None


def is_abstract_class(class_name: str, parseable_body: str) -> bool:
    return class_name in ABSTRACT_CLASSES or is_abstract_class_body(parseable_body)


def has_legacy_define_class(class_name: str) -> bool:
    pattern = re.compile(rf"\bDEFINE_CLASS\s*\(\s*{re.escape(class_name)}\s*,")
    for path in PROJECT_DIR.rglob("*"):
        if path.suffix not in {".h", ".cpp"} or should_skip(path):
            continue
        try:
            content = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        if pattern.search(content):
            return True
    return False


def parse_reflected_class(path: Path, content: str, class_match: re.Match) -> ReflectedClass:
    class_name = class_match.group("class")
    super_name = class_match.group("super")

    expected_include = f'#include "{class_name}.generated.h"'
    if expected_include not in content:
        rel_path = path.relative_to(PROJECT_DIR)
        print(f"[ReflectionGenerator] {rel_path.name}: missing {expected_include}", file=sys.stderr)
        # 에러로 중단하려면:
        fail(path, content, class_match.start(), f"missing {expected_include}")


    body_start, body_end = find_matching_class_body(path, content, class_match)
    body = content[body_start:body_end]
    parseable_body = strip_conditional_blocks(body)
    validate_generated_body(path, content, body_start, parseable_body, class_name, super_name)

    properties: list[ReflectedProperty] = []
    property_names: set[str] = set()
    for prop_match in PROPERTY_PATTERN.finditer(parseable_body):
        absolute_offset = body_start + prop_match.start()
        flag_text = prop_match.group("flags")

        raw_cpp_type = prop_match.group("type")
        cpp_type = normalize_cpp_type(raw_cpp_type)

        prop_name = prop_match.group("name")

        if prop_name in property_names:
            fail(path, content, absolute_offset, f"duplicate UPROPERTY name '{prop_name}'")
        property_names.add(prop_name)

        if ("<" in raw_cpp_type or ">" in raw_cpp_type) and not parse_array_cpp_type(cpp_type):
            fail(path, content, absolute_offset, "template UPROPERTY types are not supported")

        flag_parts = [part.strip() for part in flag_text.split(",") if part.strip()]
        property_class, object_class, inner_property_class, inner_class, inner_cpp_type, array_ops_type = reflected_type_for_flags(
            path,
            content,
            absolute_offset,
            cpp_type,
            flag_parts,
        )
        if not property_class:
            fail(path, content, absolute_offset, f"unsupported UPROPERTY type '{raw_cpp_type.strip()}'")

        properties.append(
            ReflectedProperty(
                name=prop_name,
                cpp_type=cpp_type,
                property_class=property_class,
                flags=parse_flags(path, content, absolute_offset, flag_text),
                object_class=object_class,
                inner_property_class=inner_property_class,
                inner_class=inner_class,
                inner_cpp_type=inner_cpp_type,
                array_ops_type=array_ops_type,
            )
        )

    functions: list[ReflectedFunction] = []
    function_names: set[str] = set()
    for function_match in FUNCTION_PATTERN.finditer(parseable_body):
        absolute_offset = body_start + function_match.start()
        return_type = normalize_cpp_type(function_match.group("return_type"))
        function_name = function_match.group("name")
        params = function_match.group("params").strip()

        if return_type != "void":
            fail(path, content, absolute_offset, "only void UFUNCTION return type is supported")

        if params:
            fail(path, content, absolute_offset, "UFUNCTION parameters are not supported yet")

        if function_name in function_names:
            fail(path, content, absolute_offset, f"duplicate UFUNCTION name '{function_name}'")
        function_names.add(function_name)

        functions.append(
            ReflectedFunction(
                name=function_name,
                flags=parse_function_flags(
                    path,
                    content,
                    absolute_offset,
                    function_match.group("flags"),
                ),
            )
        )

    return ReflectedClass(
        file_path=path,
        class_name=class_name,
        super_name=super_name,
        class_flags=infer_class_flags(class_name, super_name),
        is_abstract=is_abstract_class(class_name, parseable_body),
        needs_type_info_definition=not has_legacy_define_class(class_name),
        properties=properties,
        functions=functions,
    )


def parse_header(path: Path) -> list[ReflectedClass]:
    content = path.read_text(encoding="utf-8")

    class_matches = list(CLASS_PATTERN.finditer(content))
    if not class_matches:
        return []

    return [parse_reflected_class(path, content, class_match) for class_match in class_matches]


def write_text_if_changed(path: Path, content: str) -> bool:
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return False

    path.write_text(content, encoding="utf-8")
    return True


def write_generated_header(cls: ReflectedClass) -> bool:
    generated_h = f"""#pragma once

class {cls.class_name};

template<>
struct TIsUClassReflected<{cls.class_name}>
{{
    static constexpr bool Value = true;
}};
"""

    generated_path = GENERATED_DIR / f"{cls.class_name}.generated.h"
    return write_text_if_changed(generated_path, generated_h)


def build_generated_cpp(classes: list[ReflectedClass]) -> str:
    cpp_lines = [
        "// This file is generated. Do not edit manually.",
        "",
    ]

    included_files: set[Path] = set()
    for cls in classes:
        if cls.file_path in included_files:
            continue
        included_files.add(cls.file_path)
        rel_include = os.path.relpath(cls.file_path, GENERATED_DIR).replace("\\", "/")
        cpp_lines.append(f'#include "{rel_include}"')

    cpp_lines += [
        "",
        '#include "Object/Class.h"',
        '#include "Object/ObjectFactory.h"',
        '#include "Object/ReflectionRegistry.h"',
        "",
    ]

    for cls in classes:
        if cls.needs_type_info_definition:
            cpp_lines += [
                f"const FTypeInfo {cls.class_name}::s_TypeInfo = {{",
                f'    "{cls.class_name}",',
                f"    &{cls.super_name}::s_TypeInfo,",
                f"    sizeof({cls.class_name})",
                "};",
                "",
            ]

        cpp_lines += [
            f"UClass* {cls.class_name}::StaticClass()",
            "{",
            "    static UClass Class(",
            f'        "{cls.class_name}",',
            "        Super::StaticClass(),",
            f"        sizeof({cls.class_name}),",
            f"        {cls.class_flags},",
        ]

        if cls.is_abstract:
            cpp_lines += [
                "        nullptr);",
            ]
        else:
            cpp_lines += [
                "        []() -> UObject*",
                "        {",
                f"            return UObjectManager::Get().CreateObject<{cls.class_name}>();",
                "        });",
            ]

        cpp_lines += [
            "",
            "    static bool bRegistered = false;",
            "    if (!bRegistered)",
            "    {",
            "        bRegistered = true;",
            "",
        ]

        for prop in cls.properties:
            object_class_arg = (
                f"{prop.object_class}::StaticClass()"
                if prop.object_class
                else "nullptr"
            )
            inner_class_arg = (
                f"{prop.inner_class}::StaticClass()"
                if prop.inner_class
                else "nullptr"
            )
            if prop.property_class == "FArrayProperty":
                cpp_lines += [
                    f"        Class.AddProperty(MakeArrayProperty<{prop.inner_property_class}>(",
                    f'                            "{prop.name}",',
                    f"                            offsetof({cls.class_name}, {prop.name}),",
                    f"                            sizeof({prop.cpp_type}),",
                    f"                            {prop.flags},",
                    f"                            {prop.array_ops_type},",
                    f"                            {inner_class_arg}));",
                    "",
                ]
            elif prop.property_class == "FObjectProperty":
                cpp_lines += [
                    "        Class.AddProperty(MakeObjectProperty(",
                    f'                            "{prop.name}",',
                    f"                            offsetof({cls.class_name}, {prop.name}),",
                    f"                            sizeof({prop.cpp_type}),",
                    f"                            {prop.flags},",
                    f"                            {object_class_arg}));",
                    "",
                ]
            else:
                cpp_lines += [
                    f"        Class.AddProperty(MakeProperty<{prop.property_class}>(",
                    f'                            "{prop.name}",',
                    f"                            offsetof({cls.class_name}, {prop.name}),",
                    f"                            sizeof({prop.cpp_type}),",
                    f"                            {prop.flags}));",
                    "",
                ]

        for function in cls.functions:
            cpp_lines += [
                "        Class.AddFunction(MakeFunction(",
                f'                            "{function.name}",',
                f"                            {function.flags},",
                "                            [](UObject* Object)",
                "                            {",
                f"                                {cls.class_name}* TypedObject = Cast<{cls.class_name}>(Object);",
                "                                if (!TypedObject)",
                "                                {",
                "                                    return;",
                "                                }",
                "",
                f"                                TypedObject->{function.name}();",
                "                            }));",
                "",
            ]

        cpp_lines += [
            "        FReflectionRegistry::Get().RegisterUClass(&Class);",
            "    }",
            "",
            "    return &Class;",
            "}",
            "",
            "namespace",
            "{",
            f"    struct FAutoRegister{cls.class_name}",
            "    {",
            f"        FAutoRegister{cls.class_name}()",
            "        {",
            f"            {cls.class_name}::StaticClass();",
            "        }",
            "    };",
            "",
            f"    static FAutoRegister{cls.class_name} GAutoRegister{cls.class_name};",
            "}",
            "",
        ]

    return "\n".join(cpp_lines)

def main() -> int:
    classes: list[ReflectedClass] = []
    class_names: dict[str, Path] = {}
    updated_file_count = 0

    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    for file_path in sorted(PROJECT_DIR.rglob("*.h")):
        if should_skip(file_path):
            continue

        reflected_classes = parse_header(file_path)
        for reflected_class in reflected_classes:
            existing_path = class_names.get(reflected_class.class_name)
            if existing_path:
                raise ReflectionGeneratorError(
                    f"{file_path.relative_to(PROJECT_DIR)}: duplicate UCLASS name "
                    f"'{reflected_class.class_name}' already declared in "
                    f"{existing_path.relative_to(PROJECT_DIR)}"
                )
            class_names[reflected_class.class_name] = file_path
            classes.append(reflected_class)
            if write_generated_header(reflected_class):
                updated_file_count += 1

    if write_text_if_changed(GENERATED_CPP, build_generated_cpp(classes)):
        updated_file_count += 1

    print(
        f"[GenerateReflection] Generated {len(classes)} reflected classes "
        f"({updated_file_count} files updated)"
    )
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except ReflectionGeneratorError as exc:
        print(f"[GenerateReflection] error: {exc}", file=sys.stderr)
        sys.exit(1)
