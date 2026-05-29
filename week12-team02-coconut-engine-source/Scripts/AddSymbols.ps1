param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$BuildDir = "",
    [string]$StorePath = "\\172.21.11.100\Symbols",
    [string]$ProductName = "NipsEngine",
    [string]$BuildName = "",
    [string]$SymStorePath = "",
    [switch]$EnableSourceServer,
    [string]$SourceRepo = "\\172.21.11.100\SourceRepos\Week12.git",
    [string]$RepoRoot = (Join-Path $PSScriptRoot ".."),
    [string]$Commit = "",
    [ValidateSet("Snapshot", "GitCommand")]
    [string]$SourceMode = "Snapshot",
    [string]$SourceSnapshotLocalRoot = "C:\SourceRepos\Snapshots",
    [string]$SourceSnapshotShareRoot = "\\172.21.11.100\SourceRepos\Snapshots",
    [string]$GitExePath = "git.exe",
    [string]$SrcToolPath = "",
    [string]$PdbStrPath = ""
)

$ErrorActionPreference = "Stop"

function Resolve-SymStore {
    param([string]$ExplicitPath)

    if ($ExplicitPath) {
        if (Test-Path -LiteralPath $ExplicitPath) {
            return (Resolve-Path -LiteralPath $ExplicitPath).Path
        }

        throw "symstore.exe not found at '$ExplicitPath'."
    }

    $fromPath = Get-Command "symstore.exe" -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    $sdkCandidates = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64\symstore.exe",
        "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x86\symstore.exe",
        "${env:ProgramFiles}\Windows Kits\10\Debuggers\x64\symstore.exe"
    )

    foreach ($candidate in $sdkCandidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }

    throw "symstore.exe not found. Install Windows SDK Debugging Tools or pass -SymStorePath."
}

if (-not $BuildDir) {
    $BuildDir = Join-Path $PSScriptRoot "..\KraftonEngine\Bin\$Configuration"
}

$resolvedBuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
$resolvedStorePath = $StorePath

try {
    $storePathExists = Test-Path -LiteralPath $resolvedStorePath
} catch {
    throw "Cannot access symbol store '$resolvedStorePath'. Check network share credentials and write permission. Detail: $($_.Exception.Message)"
}

if (-not $storePathExists) {
    throw "Symbol store path not found: '$resolvedStorePath'. Check network share path and permissions."
}

$pdbFiles = Get-ChildItem -LiteralPath $resolvedBuildDir -Recurse -Filter "*.pdb" -File
if (-not $pdbFiles) {
    throw "No PDB files found under '$resolvedBuildDir'. Build with debug info enabled first."
}

if (-not $BuildName) {
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $BuildName = "Local-$timestamp"
}

$symstore = Resolve-SymStore -ExplicitPath $SymStorePath
$pdbPattern = Join-Path $resolvedBuildDir "*.pdb"

if ($EnableSourceServer) {
    $addSourceServer = Join-Path $PSScriptRoot "AddSourceServer.ps1"
    if (-not (Test-Path -LiteralPath $addSourceServer)) {
        throw "AddSourceServer.ps1 not found at '$addSourceServer'."
    }

    $sourceArgs = @(
        "-ExecutionPolicy", "Bypass",
        "-File", $addSourceServer,
        "-BuildDir", $resolvedBuildDir,
        "-RepoRoot", $RepoRoot,
        "-SourceRepo", $SourceRepo,
        "-SourceMode", $SourceMode,
        "-SourceSnapshotLocalRoot", $SourceSnapshotLocalRoot,
        "-SourceSnapshotShareRoot", $SourceSnapshotShareRoot,
        "-GitExePath", $GitExePath
    )

    if ($Commit) {
        $sourceArgs += @("-Commit", $Commit)
    }

    if ($SrcToolPath) {
        $sourceArgs += @("-SrcToolPath", $SrcToolPath)
    }

    if ($PdbStrPath) {
        $sourceArgs += @("-PdbStrPath", $PdbStrPath)
    }

    Write-Host "Embedding source server data before symbol registration..."
    & powershell @sourceArgs

    if ($LASTEXITCODE -ne 0) {
        throw "AddSourceServer.ps1 failed with exit code $LASTEXITCODE."
    }
}

Write-Host "SymStore : $symstore"
Write-Host "BuildDir : $resolvedBuildDir"
Write-Host "Config   : $Configuration"
Write-Host "Store    : $resolvedStorePath"
Write-Host "Product  : $ProductName"
Write-Host "Build    : $BuildName"
Write-Host "PDB Count: $($pdbFiles.Count)"

& $symstore add /r /f $pdbPattern /s $resolvedStorePath /t $ProductName /v $BuildName

if ($LASTEXITCODE -ne 0) {
    throw "symstore.exe failed with exit code $LASTEXITCODE."
}

Write-Host "Symbols added successfully."
