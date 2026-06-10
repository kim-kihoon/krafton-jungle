param(
    [Parameter(Mandatory=$true)]
    [string]$PdbFile,

    [Parameter(Mandatory=$true)]
    [string]$RepoRoot,

    [Parameter(Mandatory=$true)]
    [string]$SrcSrvPath,

    [Parameter(Mandatory=$true)]
    [string]$LogDir,

    [Parameter(Mandatory=$true)]
    [string]$VersionName
)

$ErrorActionPreference = "Stop"

function ConvertTo-GitHubRawBase {
    param(
        [string]$RemoteUrl,
        [string]$Commit
    )

    if ($RemoteUrl -match '^https://github\.com/([^/]+)/(.+?)(\.git)?$') {
        return "https://raw.githubusercontent.com/$($Matches[1])/$($Matches[2])/$Commit"
    }

    if ($RemoteUrl -match '^git@github\.com:([^/]+)/(.+?)(\.git)?$') {
        return "https://raw.githubusercontent.com/$($Matches[1])/$($Matches[2])/$Commit"
    }

    return $null
}

function ConvertTo-NormalizedRelativePath {
    param(
        [string]$FullPath,
        [string]$RootPath
    )

    try {
        $normalizedFull = [System.IO.Path]::GetFullPath($FullPath)
    }
    catch {
        return $null
    }
    $normalizedRoot = [System.IO.Path]::GetFullPath($RootPath).TrimEnd('\', '/')

    if (-not $normalizedFull.StartsWith($normalizedRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        return $null
    }

    return $normalizedFull.Substring($normalizedRoot.Length + 1).Replace('\', '/')
}

$repoRootFull = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/')
$pdbFull = [System.IO.Path]::GetFullPath($PdbFile)
$srcTool = Join-Path $SrcSrvPath "srctool.exe"
$pdbStr = Join-Path $SrcSrvPath "pdbstr.exe"

if (-not (Test-Path $pdbFull)) {
    throw "PDB not found: $pdbFull"
}

if (-not (Test-Path $srcTool)) {
    throw "srctool.exe not found: $srcTool"
}

if (-not (Test-Path $pdbStr)) {
    throw "pdbstr.exe not found: $pdbStr"
}

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$commit = (& git -C $repoRootFull rev-parse HEAD).Trim()
$remoteUrl = (& git -C $repoRootFull remote get-url origin).Trim()
$rawBase = ConvertTo-GitHubRawBase -RemoteUrl $remoteUrl -Commit $commit

if ([string]::IsNullOrWhiteSpace($rawBase)) {
    throw "Only GitHub remotes are currently supported for portable source indexing: $remoteUrl"
}

$trackedFiles = @{}
& git -C $repoRootFull ls-files | ForEach-Object {
    $trackedFiles[$_.Replace('\', '/')] = $true
}

$indexedEntries = New-Object System.Collections.Generic.List[string]
$sourceLines = & $srcTool -r $pdbFull 2>$null

foreach ($source in $sourceLines) {
    if ([string]::IsNullOrWhiteSpace($source)) {
        continue
    }

    $relativePath = ConvertTo-NormalizedRelativePath -FullPath $source -RootPath $repoRootFull
    if ([string]::IsNullOrWhiteSpace($relativePath)) {
        continue
    }

    if (-not $trackedFiles.ContainsKey($relativePath)) {
        continue
    }

    $indexedEntries.Add("$source*$relativePath")
}

if ($indexedEntries.Count -eq 0) {
    throw "No git-tracked project source files were found in PDB: $pdbFull"
}

$pdbName = [System.IO.Path]::GetFileNameWithoutExtension($pdbFull)
$srcSrvFile = Join-Path $LogDir "Release_${VersionName}_${pdbName}.srcsrv"
$streamLines = New-Object System.Collections.Generic.List[string]

$streamLines.Add("SRCSRV: ini ------------------------------------------------")
$streamLines.Add("VERSION=2")
$streamLines.Add("INDEXVERSION=2")
$streamLines.Add("VERCTRL=Git")
$streamLines.Add("DATETIME=$([DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss'))")
$streamLines.Add("SRCSRV: variables ------------------------------------------")
$streamLines.Add("GIT_COMMIT=$commit")
$streamLines.Add("GIT_REMOTE=$remoteUrl")
$streamLines.Add("RAW_BASE=$rawBase")
$streamLines.Add("SRCSRVTRG=%targ%\%var2%")
$streamLines.Add("SRCSRVCMD=powershell -NoProfile -ExecutionPolicy Bypass -Command `"New-Item -ItemType Directory -Force -Path (Split-Path -Parent '%SRCSRVTRG%') | Out-Null; Invoke-WebRequest -UseBasicParsing -Uri '%RAW_BASE%/%var2%' -OutFile '%SRCSRVTRG%'`"")
$streamLines.Add("SRCSRV: source files ---------------------------------------")
$indexedEntries | Sort-Object -Unique | ForEach-Object {
    $streamLines.Add($_)
}
$streamLines.Add("SRCSRV: end ------------------------------------------------")

[System.IO.File]::WriteAllLines($srcSrvFile, $streamLines, [System.Text.Encoding]::ASCII)

& $pdbStr -w -p:$pdbFull -i:$srcSrvFile -s:srcsrv
if ($LASTEXITCODE -ne 0) {
    throw "pdbstr.exe failed to write srcsrv stream to PDB: $pdbFull"
}

Write-Host "Source indexed PDB: $pdbFull"
Write-Host "Indexed files: $($indexedEntries.Count)"
Write-Host "SrcSrv stream: $srcSrvFile"
