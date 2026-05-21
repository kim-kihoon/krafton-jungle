param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,

    [Parameter(Mandatory = $true)]
    [string]$Configuration,

    [Parameter(Mandatory = $true)]
    [string]$Platform
)

$ErrorActionPreference = "Stop"

$ProjectRoot = [System.IO.Path]::GetFullPath($ProjectDir)
$RmlRoot = Join-Path $ProjectRoot "ThirdParty\RmlUi"
$SourceRoot = Join-Path $RmlRoot "Source"
$IncludeRoot = Join-Path $RmlRoot "Include"
$OutDir = Join-Path $RmlRoot "Lib\$Platform\$Configuration"
$ObjDir = Join-Path $ProjectRoot "Intermediate\ThirdParty\RmlUi\$Platform\$Configuration"
$LibPath = Join-Path $OutDir "RmlUiCore.lib"
$ManifestPath = Join-Path $OutDir "RmlUiCore.manifest"

$SourceDirs = @(
    "Source\Core",
    "Source\Core\Elements",
    "Source\Core\Layout"
)

function Resolve-MsvcToolPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ToolName,

        [Parameter(Mandatory = $true)]
        [string]$Platform
    )

    $ToolCommand = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($ToolCommand) {
        return $ToolCommand.Source
    }

    $TargetArch = "x64"
    if ($Platform -eq "Win32") {
        $TargetArch = "x86"
    }

    $CandidateDirs = @()
    if ($env:VCToolsInstallDir) {
        $CandidateDirs += Join-Path $env:VCToolsInstallDir "bin\Hostx64\$TargetArch"
        $CandidateDirs += Join-Path $env:VCToolsInstallDir "bin\Hostx86\$TargetArch"
    }

    if ($env:VCINSTALLDIR) {
        $ToolsDir = Join-Path $env:VCINSTALLDIR "Tools\MSVC"
        if (Test-Path $ToolsDir) {
            $LatestToolset = Get-ChildItem -Path $ToolsDir -Directory | Sort-Object Name -Descending | Select-Object -First 1
            if ($LatestToolset) {
                $CandidateDirs += Join-Path $LatestToolset.FullName "bin\Hostx64\$TargetArch"
                $CandidateDirs += Join-Path $LatestToolset.FullName "bin\Hostx86\$TargetArch"
            }
        }
    }

    foreach ($CandidateDir in $CandidateDirs) {
        $CandidatePath = Join-Path $CandidateDir $ToolName
        if (Test-Path $CandidatePath) {
            return $CandidatePath
        }
    }

    return $null
}

function New-MsvcPathArgument {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Prefix,

        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return "$Prefix`"$Path`""
}

function New-MsvcDirectoryArgument {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Prefix,

        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $FullPath = [System.IO.Path]::GetFullPath($Path)
    $NormalizedPath = $FullPath.Replace("\", "/").TrimEnd("/") + "/"
    return "$Prefix`"$NormalizedPath`""
}

function Write-MsvcResponseFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [array]$Lines
    )

    [System.IO.File]::WriteAllLines(
        [System.IO.Path]::GetFullPath($Path),
        [string[]]$Lines,
        [System.Text.Encoding]::Unicode)
}

function Write-Utf8TextFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Text
    )

    [System.IO.File]::WriteAllText(
        [System.IO.Path]::GetFullPath($Path),
        $Text,
        [System.Text.Encoding]::UTF8)
}

function Get-RelativePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BasePath,

        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $BaseFullPath = [System.IO.Path]::GetFullPath($BasePath)
    if (-not $BaseFullPath.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
        $BaseFullPath += [System.IO.Path]::DirectorySeparatorChar
    }

    $BaseUri = New-Object System.Uri($BaseFullPath)
    $PathUri = New-Object System.Uri([System.IO.Path]::GetFullPath($Path))
    return [System.Uri]::UnescapeDataString($BaseUri.MakeRelativeUri($PathUri).ToString()).Replace("/", "\")
}

function Get-UniqueFiles {
    param(
        [Parameter(Mandatory = $true)]
        [array]$Files
    )

    $Seen = @{}
    $Result = @()
    foreach ($File in $Files) {
        $FullName = [System.IO.Path]::GetFullPath($File.FullName)
        if (-not $Seen.ContainsKey($FullName)) {
            $Seen[$FullName] = $true
            $Result += $File
        }
    }

    return $Result
}

function Get-FileSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $Sha256 = [System.Security.Cryptography.SHA256]::Create()
    $Stream = [System.IO.File]::OpenRead($Path)
    try {
        $HashBytes = $Sha256.ComputeHash($Stream)
        return ([System.BitConverter]::ToString($HashBytes) -replace "-", "").ToLowerInvariant()
    }
    finally {
        $Stream.Dispose()
        $Sha256.Dispose()
    }
}

function New-InputManifest {
    param(
        [Parameter(Mandatory = $true)]
        [array]$Files,

        [Parameter(Mandatory = $true)]
        [array]$BuildSettings
    )

    $Lines = @()
    $Lines += "ManifestVersion=2"
    $Lines += $BuildSettings

    foreach ($File in ($Files | Sort-Object FullName)) {
        $RelativePath = Get-RelativePath -BasePath $ProjectRoot -Path $File.FullName
        $Hash = Get-FileSha256 -Path $File.FullName
        $Lines += "Input=$RelativePath|$($File.Length)|$Hash"
    }

    return ($Lines -join "`n")
}

$CppFiles = @()
foreach ($SourceDir in $SourceDirs) {
    $FullSourceDir = Join-Path $RmlRoot $SourceDir
    if (Test-Path $FullSourceDir) {
        $CppFiles += Get-ChildItem -Path $FullSourceDir -Filter "*.cpp" -File
    }
}

$ClPath = Resolve-MsvcToolPath -ToolName "cl.exe" -Platform $Platform
$LibToolPath = Resolve-MsvcToolPath -ToolName "lib.exe" -Platform $Platform
if (-not $ClPath -or -not $LibToolPath) {
    throw "cl.exe and lib.exe must be available. Install the Visual Studio C++ workload and build through Visual Studio, MSBuild, or a Visual Studio developer command prompt."
}

if ($CppFiles.Count -eq 0) {
    throw "No RmlUi source files were found under $SourceRoot."
}

$RuntimeFlag = "/MD"
if ($Configuration -eq "Debug") {
    $RuntimeFlag = "/MDd"
}

$Defines = @(
    "/DRMLUI_STATIC_LIB",
    "/D_CRT_SECURE_NO_WARNINGS",
    "/DNOMINMAX"
)

if ($Configuration -eq "Debug") {
    $Defines += "/D_DEBUG"
}
else {
    $Defines += "/DNDEBUG"
}

$CommonArgs = @(
    "/nologo",
    "/c",
    "/std:c++20",
    "/permissive-",
    "/EHa",
    "/W3",
    "/utf-8",
    "/bigobj",
    $RuntimeFlag,
    (New-MsvcPathArgument -Prefix "/I" -Path $IncludeRoot),
    (New-MsvcPathArgument -Prefix "/I" -Path $SourceRoot),
    (New-MsvcPathArgument -Prefix "/I" -Path $RmlRoot)
) + $Defines

$ManifestCompileArgs = @(
    "/nologo",
    "/c",
    "/std:c++20",
    "/permissive-",
    "/EHa",
    "/W3",
    "/utf-8",
    "/bigobj",
    $RuntimeFlag,
    "/IThirdParty\RmlUi\Include",
    "/IThirdParty\RmlUi\Source",
    "/IThirdParty\RmlUi"
) + $Defines

$InputFiles = @()
$InputFiles += $CppFiles
$InputFiles += Get-ChildItem -Path $IncludeRoot -Include "*.h", "*.hpp" -Recurse -File
$InputFiles += Get-ChildItem -Path $SourceRoot -Include "*.h", "*.hpp" -Recurse -File
$InputFiles = Get-UniqueFiles -Files $InputFiles

$BuildSettings = @(
    "Library=RmlUiCore",
    "Configuration=$Configuration",
    "Platform=$Platform",
    "Runtime=$RuntimeFlag",
    "SourceDirs=$($SourceDirs -join ';')",
    "CompileArgs=$($ManifestCompileArgs -join ';')"
)
$InputManifest = New-InputManifest -Files $InputFiles -BuildSettings $BuildSettings

$NeedsBuild = -not (Test-Path $LibPath)
if (-not $NeedsBuild) {
    if (-not (Test-Path $ManifestPath)) {
        $NeedsBuild = $true
    }
    else {
        $ExistingManifest = (Get-Content -Path $ManifestPath -Raw).Trim()
        if ($ExistingManifest -ne $InputManifest.Trim()) {
            $NeedsBuild = $true
        }
    }
}

if (-not $NeedsBuild) {
    Write-Host "RmlUiCore.lib is up to date: $LibPath"
    exit 0
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null

$ObjectFiles = @()
foreach ($CppFile in $CppFiles) {
    $ObjectFiles += Join-Path $ObjDir ([System.IO.Path]::ChangeExtension($CppFile.Name, ".obj"))
}

$CompileResponsePath = Join-Path $ObjDir "RmlUiCore.cl.rsp"
$CompileResponse = @()
$CompileResponse += $CommonArgs
$CompileResponse += (New-MsvcDirectoryArgument -Prefix "/Fo" -Path $ObjDir)
foreach ($CppFile in $CppFiles) {
    $CompileResponse += "`"$($CppFile.FullName)`""
}

Write-MsvcResponseFile -Path $CompileResponsePath -Lines $CompileResponse

& $ClPath "@$CompileResponsePath"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to compile RmlUi static library sources."
}

$LibResponsePath = Join-Path $ObjDir "RmlUiCore.lib.rsp"
$LibResponse = @()
$LibResponse += (New-MsvcPathArgument -Prefix "/OUT:" -Path $LibPath)
foreach ($ObjectFile in $ObjectFiles) {
    $LibResponse += "`"$ObjectFile`""
}

Write-MsvcResponseFile -Path $LibResponsePath -Lines $LibResponse

& $LibToolPath /nologo "@$LibResponsePath"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to create $LibPath."
}

Write-Utf8TextFile -Path $ManifestPath -Text $InputManifest
Write-Host "Built RmlUiCore.lib: $LibPath"
