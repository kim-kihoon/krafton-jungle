"""
Generate Visual Studio project files for JSEngine from the on-disk source tree.

The generated files are:
  - JSEngine.sln
  - JSEngine/JSEngine.vcxproj
  - JSEngine/JSEngine.vcxproj.filters
"""

from __future__ import annotations

import hashlib
import os
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
PROJECT_DIR_NAME = "JSEngine"
PROJECT_DIR = ROOT / PROJECT_DIR_NAME

SOLUTION_NAME = "JSEngine"
PROJECT_NAME = "JSEngine"
PROJECT_GUID = "{55068e81-c0a0-49f9-ab7b-54aea968722b}"
SOLUTION_GUID = "{4EBC5DD2-CECA-4722-9D19-87C7CB5F481B}"
VS_PROJECT_TYPE = "{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}"
ROOT_NAMESPACE = "Week2"

# 실제 C++ 프로젝트 구성. 솔루션 전용 alias는 여기에 추가하지 않음
PROJECT_CONFIGURATIONS = [
    ("Debug", "Win32"),
    ("Release", "Win32"),
    ("Debug", "x64"),
    ("Release", "x64"),
    ("ObjViewer", "x64"),
    ("GameClientDebug", "x64"),
    ("GameClientRelease", "x64"),
]

# Visual Studio 툴바에서 x86을 선택할 수 있으므로, x64 전용 구성도 솔루션에는
# x86 alias 노출. 실제 프로젝트 구성 매핑은 아래 함수에서 처리
SOLUTION_CONFIGURATIONS = [
    ("Debug", "x64"),
    ("Debug", "x86"),
    ("GameClientDebug", "x64"),
    ("GameClientDebug", "x86"),
    ("GameClientRelease", "x64"),
    ("GameClientRelease", "x86"),
    ("ObjViewer", "x64"),
    ("ObjViewer", "x86"),
    ("Release", "x64"),
    ("Release", "x86"),
]

# RmlUi와 SoLoud는 여기서 재귀 스캔하지 않음. 두 라이브러리는
# BuildTools/Scripts/*.ps1에서 정적 라이브러리로 빌드한 뒤 vcxproj에서 링크
SOURCE_SCAN_DIRS = ["Source", "ThirdParty\\ImGui", "ThirdParty\\imgui-node-editor"]
SHADER_SCAN_DIRS = ["Shaders"]
ROOT_SOURCE_FILES = ["main.cpp", "TestComponent.cpp"]
GENERATED_SOURCE_FILES = ["Generated\\JSEngine.generated.cpp"]

# scan 대상 밖에 있지만 프로젝트에 직접 포함할 개별 source 파일
EXPLICIT_SOURCE_FILES = []

# scan 대상 밖에 있지만 Solution Explorer에 표시할 header-only 또는 외부 빌드 진입점
EXPLICIT_HEADER_FILES = [
    "TestComponent.h",
    "ThirdParty\\luajit\\src\\lua.h",
    "ThirdParty\\SimpleJSON\\json.hpp",
]

SOURCE_EXTS = {".cpp", ".c", ".cc", ".cxx"}
HEADER_EXTS = {".h", ".hpp", ".hxx", ".inl"}
SHADER_EXTS = {".hlsl", ".hlsli"}
NATVIS_EXTS = {".natvis"}
NONE_EXTS = {".natstepfilter", ".config"}
RESOURCE_EXTS = {".rc"}

BASE_INCLUDE_PATHS = [
    "Generated",
    "Source\\Engine",
    "Source",
    "ThirdParty",
    "ThirdParty\\SoLoud\\include",
    "ThirdParty\\RmlUi\\Include",
    "ThirdParty\\ImGui",
    "Source\\Editor",
    ".",
]

# GameClient는 대부분의 에디터 소스를 컴파일에서 제외하지만, 일부 런타임 파일은
# 공유 선언 때문에 에디터 헤더를 include함
GAME_CLIENT_INCLUDE_PATHS = [
    "Generated",
    "Source\\Engine",
    "Source",
    "ThirdParty",
    "ThirdParty\\SoLoud\\include",
    "ThirdParty\\RmlUi\\Include",
    "ThirdParty\\ImGui",
    ".",
]

X64_ADDITIONAL_INCLUDE_DIRS = [
    "$(ProjectDir)ThirdParty\\luajit\\src",
    "$(ProjectDir)ThirdParty\\SoLoud\\include",
    "$(ProjectDir)ThirdParty\\RmlUi\\Include",
    "$(ProjectDir)ThirdParty",
    "$(ProjectDir)ThirdParty\\FBX\\include",
    "%(AdditionalIncludeDirectories)",
]

OBJ_VIEWER_ADDITIONAL_INCLUDE_DIRS = [
    "$(ProjectDir)ThirdParty\\SoLoud\\include",
    "$(ProjectDir)ThirdParty\\RmlUi\\Include",
    "$(ProjectDir)ThirdParty",
    "%(AdditionalIncludeDirectories)",
]

GAME_CLIENT_ADDITIONAL_INCLUDE_DIRS = [
    "$(ProjectDir)Source\\Editor",
    *X64_ADDITIONAL_INCLUDE_DIRS,
]

NUGET_PACKAGES = [
    ("directxtk_desktop_win10", "2025.10.28.2"),
]

# Visual Studio 필터는 표시용. 파일을 프로젝트에 직접 포함하지 않는 외부
# 라이브러리 루트도 Solution Explorer에서 보이도록 유지
FILTER_ONLY_FOLDERS = [
    "ThirdParty\\SoLoud\\",
]

MSBUILD_NS = "http://schemas.microsoft.com/developer/msbuild/2003"


def normalize_rel(path: Path) -> str:
    return str(path).replace("/", "\\")


def condition(config: str, platform: str) -> str:
    return f"'$(Configuration)|$(Platform)'=='{config}|{platform}'"


def is_release_like(config: str) -> bool:
    return config in {"Release", "ObjViewer", "GameClientRelease"}


def is_game_client(config: str) -> bool:
    return config.startswith("GameClient")


def scan_files(project_dir: Path) -> dict[str, list[str]]:
    result: dict[str, list[str]] = {
        "ClCompile": [],
        "ClInclude": [],
        "None": [],
        "Natvis": [],
        "ResourceCompile": [],
        "Folder": [],
    }

    # 재생성된 프로젝트 파일 diff가 안정적이도록 항상 결정적인 순서로 스캔
    for scan_dir in SOURCE_SCAN_DIRS:
        full_dir = project_dir / scan_dir
        if not full_dir.exists():
            continue

        for dirpath, dirnames, filenames in os.walk(full_dir):
            dirnames.sort()
            for filename in sorted(filenames):
                full_path = Path(dirpath) / filename
                rel = normalize_rel(full_path.relative_to(project_dir))
                ext = full_path.suffix.lower()

                if ext in SOURCE_EXTS:
                    result["ClCompile"].append(rel)
                elif ext in HEADER_EXTS:
                    result["ClInclude"].append(rel)
                elif ext in RESOURCE_EXTS:
                    result["ResourceCompile"].append(rel)
                elif ext in NATVIS_EXTS:
                    result["Natvis"].append(rel)
                elif ext in NONE_EXTS:
                    result["None"].append(rel)

    # 셰이더는 런타임 에셋이며 Visual Studio FxCompile 빌드 입력이 아님
    for shader_dir in SHADER_SCAN_DIRS:
        full_dir = project_dir / shader_dir
        if not full_dir.exists():
            continue

        for dirpath, dirnames, filenames in os.walk(full_dir):
            dirnames.sort()
            rel_dir = normalize_rel(Path(dirpath).relative_to(project_dir))
            if rel_dir != ".":
                result["Folder"].append(rel_dir.rstrip("\\") + "\\")

            for filename in sorted(filenames):
                full_path = Path(dirpath) / filename
                if full_path.suffix.lower() in SHADER_EXTS:
                    result["None"].append(normalize_rel(full_path.relative_to(project_dir)))

    for source_file in [*GENERATED_SOURCE_FILES, *ROOT_SOURCE_FILES, *EXPLICIT_SOURCE_FILES]:
        if (project_dir / source_file).exists():
            result["ClCompile"].append(source_file.replace("/", "\\"))

    for header_file in EXPLICIT_HEADER_FILES:
        if (project_dir / header_file).exists():
            result["ClInclude"].append(header_file.replace("/", "\\"))

    for folder in FILTER_ONLY_FOLDERS:
        result["Folder"].append(folder)

    for key in result:
        result[key] = sorted(dict.fromkeys(result[key]))

    return result


def get_filter(rel_path: str) -> str:
    parts = rel_path.replace("/", "\\").rstrip("\\").rsplit("\\", 1)
    return parts[0] if len(parts) > 1 else ""


def collect_all_filters(files: dict[str, list[str]]) -> set[str]:
    filters: set[str] = set()
    for item_type, file_list in files.items():
        if item_type == "Folder":
            for folder in file_list:
                clean = folder.rstrip("\\")
                if clean:
                    parts = clean.split("\\")
                    for index in range(1, len(parts) + 1):
                        filters.add("\\".join(parts[:index]))
            continue

        for rel in file_list:
            filt = get_filter(rel)
            if not filt:
                continue
            parts = filt.split("\\")
            for index in range(1, len(parts) + 1):
                filters.add("\\".join(parts[:index]))
    return filters


def indent_xml(elem: ET.Element, level: int = 0) -> None:
    indent = "\n" + "  " * level
    if len(elem):
        if not elem.text or not elem.text.strip():
            elem.text = indent + "  "
        for child in elem:
            indent_xml(child, level + 1)
        if not elem[-1].tail or not elem[-1].tail.strip():
            elem[-1].tail = indent
    if level and (not elem.tail or not elem.tail.strip()):
        elem.tail = indent


def write_xml(root_elem: ET.Element, path: Path, bom: bool = False) -> None:
    indent_xml(root_elem)
    encoding = "utf-8-sig" if bom else "utf-8"
    with open(path, "w", encoding=encoding, newline="\r\n") as file:
        file.write('<?xml version="1.0" encoding="utf-8"?>\n')
        ET.ElementTree(root_elem).write(file, encoding="unicode", xml_declaration=False)
        file.write("\n")


def add_text(parent: ET.Element, tag: str, text: str | None = None, **attrs: str) -> ET.Element:
    elem = ET.SubElement(parent, tag, attrs)
    if text is not None:
        elem.text = text
    return elem


def add_excluded_from_build(parent: ET.Element, excluded_configs: list[tuple[str, str]]) -> None:
    for config, platform in excluded_configs:
        add_text(parent, "ExcludedFromBuild", "true", Condition=condition(config, platform))


def game_client_exclusions_for_source(rel_path: str) -> list[tuple[str, str]]:
    # GameClient 빌드는 엔진/런타임만 링크함. Editor 및 ObjViewer 코드는
    # 프로젝트에는 표시하되 게임 구성에서는 빌드 제외
    if rel_path.startswith("Source\\Editor\\") or rel_path.startswith("Source\\Misc\\ObjViewer\\"):
        return [("GameClientDebug", "x64"), ("GameClientRelease", "x64")]
    return []


def resource_exclusions(rel_path: str) -> list[tuple[str, str]]:
    # AppIcon.rc는 에디터 빌드용. GameBranding.rc는 에디터 패키저가
    # 생성하므로 일반 에디터 Release/Debug 빌드에서는 제외
    if rel_path == "Source\\Engine\\Runtime\\AppIcon.rc":
        return [
            ("Debug", "Win32"),
            ("Release", "Win32"),
            ("ObjViewer", "x64"),
            ("GameClientDebug", "x64"),
            ("GameClientRelease", "x64"),
        ]
    if rel_path == "Source\\Engine\\Runtime\\GameBranding.rc":
        return [("Debug", "x64"), ("Release", "x64")]
    return []


def include_path_for(config: str) -> str:
    paths = GAME_CLIENT_INCLUDE_PATHS if is_game_client(config) else BASE_INCLUDE_PATHS
    return ";".join(paths) + ";$(IncludePath)"


def preprocessor_definitions(config: str, platform: str) -> str:
    # C++ 코드의 런타임 feature gate와 일치하도록 전처리 정의 유지
    prefix = []
    if platform == "Win32":
        prefix.append("WIN32")

    if config == "Debug":
        prefix.append("_DEBUG")
    else:
        prefix.append("NDEBUG")

    prefix.append("_CONSOLE")
    prefix.append("WITH_EDITOR=0" if is_game_client(config) else "WITH_EDITOR=1")

    if is_game_client(config):
        prefix.extend(["IS_GAME_CLIENT=1", "IS_OBJ_VIEWER=0"])
        if config == "GameClientDebug":
            prefix.append("GAME_DEVELOPMENT=1")
    elif config == "ObjViewer":
        prefix.append("IS_OBJ_VIEWER=1")

    prefix.extend(["WITH_MINIAUDIO", "RMLUI_STATIC_LIB", "_CRT_SECURE_NO_WARNINGS", "NOMINMAX"])

    if platform == "x64":
        prefix.extend(["SOL_LUA_VERSION=501", "SOL_LUAJIT=1"])

    prefix.append("%(PreprocessorDefinitions)")
    return ";".join(prefix) + ";"


def add_item_definition_group(project: ET.Element, config: str, platform: str) -> None:
    idg = add_text(project, "ItemDefinitionGroup", Condition=condition(config, platform))
    cl = add_text(idg, "ClCompile")

    add_text(cl, "WarningLevel", "Level3")
    if is_release_like(config):
        add_text(cl, "FunctionLevelLinking", "true")
        add_text(cl, "IntrinsicFunctions", "true")
    add_text(cl, "SDLCheck", "true")
    add_text(cl, "PreprocessorDefinitions", preprocessor_definitions(config, platform))
    add_text(cl, "MultiProcessorCompilation", "true")
    add_text(cl, "ConformanceMode", "true")
    add_text(cl, "AdditionalOptions", "/utf-8 /bigobj %(AdditionalOptions)")
    add_text(cl, "ExceptionHandling", "Async")

    if platform == "x64":
        # x64 구성은 LuaJIT/FBX/RmlUi/SoLoud 사용. ObjViewer는
        # LuaJIT/FBX include set을 쓰지 않으므로 더 좁은 include 목록 사용
        add_text(cl, "LanguageStandard", "stdcpp20")
        if config == "ObjViewer":
            include_dirs = OBJ_VIEWER_ADDITIONAL_INCLUDE_DIRS
        elif is_game_client(config):
            include_dirs = GAME_CLIENT_ADDITIONAL_INCLUDE_DIRS
        else:
            include_dirs = X64_ADDITIONAL_INCLUDE_DIRS
        add_text(cl, "AdditionalIncludeDirectories", ";".join(include_dirs))

    link = add_text(idg, "Link")
    add_text(link, "SubSystem", "Windows" if platform == "x64" else "Console")
    add_text(link, "GenerateDebugInformation", "true")
    if platform == "x64" and config != "ObjViewer":
        add_text(
            link,
            "AdditionalLibraryDirectories",
            "$(ProjectDir)ThirdParty\\luajit\\src;%(AdditionalLibraryDirectories)",
        )
        add_text(link, "AdditionalDependencies", "gdiplus.lib;lua51.lib;%(AdditionalDependencies)")


def generate_vcxproj(files: dict[str, list[str]]) -> None:
    project = ET.Element("Project", DefaultTargets="Build", xmlns=MSBUILD_NS)

    item_group = add_text(project, "ItemGroup", Label="ProjectConfigurations")
    for config, platform in PROJECT_CONFIGURATIONS:
        pc = add_text(item_group, "ProjectConfiguration", Include=f"{config}|{platform}")
        add_text(pc, "Configuration", config)
        add_text(pc, "Platform", platform)

    globals_group = add_text(project, "PropertyGroup", Label="Globals")
    add_text(globals_group, "VCProjectVersion", "17.0")
    add_text(globals_group, "Keyword", "Win32Proj")
    add_text(globals_group, "ProjectGuid", PROJECT_GUID)
    add_text(globals_group, "RootNamespace", ROOT_NAMESPACE)
    add_text(globals_group, "WindowsTargetPlatformVersion", "10.0")

    add_text(project, "Import", Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props")

    for config, platform in PROJECT_CONFIGURATIONS:
        group = add_text(project, "PropertyGroup", Condition=condition(config, platform), Label="Configuration")
        add_text(group, "ConfigurationType", "Application")
        add_text(group, "UseDebugLibraries", "true" if config == "Debug" else "false")
        add_text(group, "PlatformToolset", "v143")
        if is_release_like(config):
            add_text(group, "WholeProgramOptimization", "true")
        add_text(group, "CharacterSet", "Unicode")

    add_text(project, "Import", Project="$(VCTargetsPath)\\Microsoft.Cpp.props")
    add_text(project, "ImportGroup", Label="ExtensionSettings")
    add_text(project, "ImportGroup", Label="Shared")

    for config, platform in PROJECT_CONFIGURATIONS:
        group = add_text(project, "ImportGroup", Label="PropertySheets", Condition=condition(config, platform))
        add_text(
            group,
            "Import",
            Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props",
            Condition="exists('$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props')",
            Label="LocalAppDataPlatform",
        )

    add_text(project, "PropertyGroup", Label="UserMacros")

    for config, platform in PROJECT_CONFIGURATIONS:
        group = add_text(project, "PropertyGroup", Condition=condition(config, platform))
        add_text(group, "OutDir", "$(ProjectDir)Bin\\$(Configuration)\\")
        add_text(group, "IntDir", "$(ProjectDir)Build\\$(Configuration)\\")
        if is_game_client(config):
            add_text(group, "TargetName", "JSEngineGame")
        add_text(group, "IncludePath", include_path_for(config))
        add_text(group, "LibraryPath", "$(LibraryPath)")
        add_text(group, "LocalDebuggerWorkingDirectory", "$(ProjectDir)")

    for config, platform in PROJECT_CONFIGURATIONS:
        add_item_definition_group(project, config, platform)

    # RmlUi, SoLoud, FBX 공통 링크 설정. 라이브러리 구성명은 아래
    # ThirdPartyBinaryConfiguration에서 정규화
    common = add_text(project, "ItemDefinitionGroup")
    common_cl = add_text(common, "ClCompile")
    add_text(common_cl, "PreprocessorDefinitions", "FBXSDK_SHARED;%(PreprocessorDefinitions)")
    common_link = add_text(common, "Link")
    add_text(
        common_link,
        "AdditionalLibraryDirectories",
        "$(ProjectDir)ThirdParty\\RmlUi\\Lib\\$(Platform)\\$(ThirdPartyBinaryConfiguration);"
        "$(ProjectDir)ThirdParty\\SoLoud\\Lib\\$(Platform)\\$(ThirdPartyBinaryConfiguration);"
        "$(ProjectDir)ThirdParty\\FBX\\lib\\$(FbxSdkConfiguration);"
        "%(AdditionalLibraryDirectories)",
    )
    add_text(common_link, "AdditionalDependencies", "RmlUiCore.lib;SoLoud.lib;libfbxsdk.lib;%(AdditionalDependencies)")
    post_build = add_text(common, "PostBuildEvent")
    add_text(post_build, "Command", 'xcopy /Y "$(ProjectDir)ThirdParty\\FBX\\lib\\$(FbxSdkConfiguration)\\libfbxsdk.dll" "$(OutDir)"')

    third_party_props = add_text(project, "PropertyGroup")
    add_text(third_party_props, "ThirdPartyBinaryConfiguration", "Release")
    add_text(third_party_props, "ThirdPartyBinaryConfiguration", "Debug", Condition="'$(Configuration)'=='Debug'")
    add_text(third_party_props, "FbxSdkConfiguration", "release")
    add_text(third_party_props, "FbxSdkConfiguration", "debug", Condition="'$(Configuration)'=='Debug'")

    # 프로젝트 소스 컴파일 전에 ThirdParty 정적 라이브러리를 먼저 빌드
    rml_target = add_text(project, "Target", Name="BuildRmlUiStaticLibrary", BeforeTargets="ClCompile")
    add_text(
        rml_target,
        "Exec",
        Command='powershell -NoProfile -ExecutionPolicy Bypass -File "$(MSBuildProjectDirectory)\\BuildTools\\Scripts\\BuildRmlUiLib.ps1" -ProjectDir "$(MSBuildProjectDirectory)" -Configuration "$(ThirdPartyBinaryConfiguration)" -Platform "$(Platform)"',
    )

    reflection_target = add_text(project, "Target", Name="GenerateReflectionCode", BeforeTargets="ClCompile")
    add_text(
        reflection_target,
        "Exec",
        Command='python "$(MSBuildProjectDirectory)\\BuildTools\\Scripts\\GenerateReflection.py"',
    )

    soloud_target = add_text(project, "Target", Name="BuildSoLoudStaticLibrary", BeforeTargets="ClCompile")
    add_text(
        soloud_target,
        "Exec",
        Command='powershell -NoProfile -ExecutionPolicy Bypass -File "$(MSBuildProjectDirectory)\\BuildTools\\Scripts\\BuildSoLoudLib.ps1" -ProjectDir "$(MSBuildProjectDirectory)" -Configuration "$(ThirdPartyBinaryConfiguration)" -Platform "$(Platform)"',
    )

    compile_group = add_text(project, "ItemGroup")
    for rel in files["ClCompile"]:
        item = add_text(compile_group, "ClCompile", Include=rel)
        add_excluded_from_build(item, game_client_exclusions_for_source(rel))
        if rel == "Source\\Engine\\Runtime\\Script\\ScriptManager.cpp":
            add_text(item, "AdditionalOptions", "/bigobj %(AdditionalOptions)")

    include_group = add_text(project, "ItemGroup")
    for rel in files["ClInclude"]:
        add_text(include_group, "ClInclude", Include=rel)

    if files["ResourceCompile"]:
        resource_group = add_text(project, "ItemGroup")
        for rel in files["ResourceCompile"]:
            item = add_text(resource_group, "ResourceCompile", Include=rel)
            add_excluded_from_build(item, resource_exclusions(rel))

    if files["None"]:
        none_group = add_text(project, "ItemGroup")
        for rel in files["None"]:
            add_text(none_group, "None", Include=rel)

    if files["Natvis"]:
        natvis_group = add_text(project, "ItemGroup")
        for rel in files["Natvis"]:
            add_text(natvis_group, "Natvis", Include=rel)

    if files["Folder"]:
        folder_group = add_text(project, "ItemGroup")
        for rel in files["Folder"]:
            add_text(folder_group, "Folder", Include=rel)

    add_text(project, "Import", Project="$(VCTargetsPath)\\Microsoft.Cpp.targets")

    # LuaJIT은 ThirdParty/luajit/src에서 링크되며 실행 파일 옆에 있어야 함
    lua_target = add_text(
        project,
        "Target",
        Name="CopyLuaJitRuntimeDll",
        AfterTargets="Build",
        Condition="Exists('$(ProjectDir)ThirdParty\\luajit\\src\\lua51.dll')",
    )
    add_text(
        lua_target,
        "Copy",
        SourceFiles="$(ProjectDir)ThirdParty\\luajit\\src\\lua51.dll",
        DestinationFolder="$(OutDir)",
        SkipUnchangedFiles="true",
    )

    add_nuget_targets(project)

    write_xml(project, PROJECT_DIR / f"{PROJECT_NAME}.vcxproj")


def add_nuget_targets(project: ET.Element) -> None:
    extension_targets = add_text(project, "ImportGroup", Label="ExtensionTargets")
    for package_id, package_version in NUGET_PACKAGES:
        targets_path = f"packages\\{package_id}.{package_version}\\build\\native\\{package_id}.targets"
        add_text(extension_targets, "Import", Project=targets_path, Condition=f"Exists('{targets_path}')")

    ensure = add_text(project, "Target", Name="EnsureNuGetPackageBuildImports", BeforeTargets="PrepareForBuild")
    property_group = add_text(ensure, "PropertyGroup")
    add_text(
        property_group,
        "ErrorText",
        "This project references NuGet package(s) that are missing on this computer. "
        "Use NuGet Package Restore to download them.  For more information, see "
        "http://go.microsoft.com/fwlink/?LinkID=322105. The missing file is {0}.",
    )
    for package_id, package_version in NUGET_PACKAGES:
        targets_path = f"packages\\{package_id}.{package_version}\\build\\native\\{package_id}.targets"
        add_text(
            ensure,
            "Error",
            Condition=f"!Exists('{targets_path}')",
            Text=f"$([System.String]::Format('$(ErrorText)', '{targets_path}'))",
        )


def generate_filters(files: dict[str, list[str]]) -> None:
    project = ET.Element("Project", ToolsVersion="4.0", xmlns=MSBUILD_NS)

    all_filters = collect_all_filters(files)
    if all_filters:
        group = add_text(project, "ItemGroup")
        for filt in sorted(all_filters):
            item = add_text(group, "Filter", Include=filt)
            digest = hashlib.md5(f"{PROJECT_NAME}:{filt}".encode("utf-8")).hexdigest()
            guid = f"{{{digest[:8]}-{digest[8:12]}-{digest[12:16]}-{digest[16:20]}-{digest[20:]}}}"
            add_text(item, "UniqueIdentifier", guid)

    for item_type in ["ClCompile", "ClInclude", "ResourceCompile", "None", "Natvis"]:
        if not files[item_type]:
            continue
        group = add_text(project, "ItemGroup")
        for rel in files[item_type]:
            item = add_text(group, item_type, Include=rel)
            filt = get_filter(rel)
            if filt:
                add_text(item, "Filter", filt)

    write_xml(project, PROJECT_DIR / f"{PROJECT_NAME}.vcxproj.filters", bom=True)


def map_solution_config_to_project(config: str, solution_platform: str) -> tuple[str, str]:
    # x64 전용 타깃의 x86 솔루션 항목은 의도적으로 x64 프로젝트 구성을 빌드
    if solution_platform == "x64":
        return config, "x64"
    if config in {"Debug", "Release"}:
        return config, "Win32"
    return config, "x64"


def generate_sln() -> None:
    guid_upper = PROJECT_GUID.upper()
    lines = [
        "",
        "Microsoft Visual Studio Solution File, Format Version 12.00",
        "# Visual Studio Version 17",
        "VisualStudioVersion = 17.14.37012.4",
        "MinimumVisualStudioVersion = 10.0.40219.1",
        f'Project("{VS_PROJECT_TYPE}") = "{PROJECT_NAME}", "{PROJECT_DIR_NAME}\\{PROJECT_NAME}.vcxproj", "{guid_upper}"',
        "EndProject",
        "Global",
        "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution",
    ]

    for config, platform in SOLUTION_CONFIGURATIONS:
        lines.append(f"\t\t{config}|{platform} = {config}|{platform}")

    lines.extend([
        "\tEndGlobalSection",
        "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution",
    ])

    for config, solution_platform in SOLUTION_CONFIGURATIONS:
        project_config, project_platform = map_solution_config_to_project(config, solution_platform)
        lines.append(f"\t\t{guid_upper}.{config}|{solution_platform}.ActiveCfg = {project_config}|{project_platform}")
        lines.append(f"\t\t{guid_upper}.{config}|{solution_platform}.Build.0 = {project_config}|{project_platform}")

    lines.extend([
        "\tEndGlobalSection",
        "\tGlobalSection(SolutionProperties) = preSolution",
        "\t\tHideSolutionNode = FALSE",
        "\tEndGlobalSection",
        "\tGlobalSection(ExtensibilityGlobals) = postSolution",
        f"\t\tSolutionGuid = {SOLUTION_GUID}",
        "\tEndGlobalSection",
        "EndGlobal",
        "",
    ])

    with open(ROOT / f"{SOLUTION_NAME}.sln", "w", encoding="utf-8-sig", newline="\r\n") as file:
        file.write("\n".join(lines))


def main() -> int:
    print(f"Scanning project files in {PROJECT_DIR}...")
    files = scan_files(PROJECT_DIR)

    for item_type in ["ClCompile", "ClInclude", "ResourceCompile", "None", "Natvis", "Folder"]:
        print(f"  {item_type}: {len(files[item_type])} items")

    print("Generating project files...")
    generate_vcxproj(files)
    print(f"  {PROJECT_DIR_NAME}\\{PROJECT_NAME}.vcxproj")
    generate_filters(files)
    print(f"  {PROJECT_DIR_NAME}\\{PROJECT_NAME}.vcxproj.filters")
    generate_sln()
    print(f"  {SOLUTION_NAME}.sln")
    print("Done.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
