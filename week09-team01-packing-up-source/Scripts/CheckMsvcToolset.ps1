param(
    [int]$MinMajor = 14,
    [int]$MinMinor = 44
)

$roots = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC",
    "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC",
    "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\MSVC",
    "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Tools\MSVC"
)

$versions = foreach ($root in $roots) {
    if (Test-Path $root) {
        Get-ChildItem $root -Directory | ForEach-Object { $_.Name }
    }
}

$versions = $versions | Sort-Object -Unique

if (-not $versions) {
    Write-Host "ERROR: No MSVC toolset was found."
    Write-Host "Install Visual Studio 2022 with MSVC v143 x64/x86 build tools."
    exit 1
}

Write-Host "Detected MSVC toolsets:"
$versions | ForEach-Object { Write-Host "  $_" }

$ok = $false
foreach ($version in $versions) {
    $parts = $version.Split(".")
    if ($parts.Count -lt 2) {
        continue
    }

    $major = [int]$parts[0]
    $minor = [int]$parts[1]
    if ($major -gt $MinMajor -or ($major -eq $MinMajor -and $minor -ge $MinMinor)) {
        $ok = $true
    }
}

if (-not $ok) {
    Write-Host ""
    Write-Host "ERROR: MSVC toolset $MinMajor.$MinMinor or newer is required for Jolt Physics."
    Write-Host "Install or update MSVC v143 x64/x86 build tools in Visual Studio Installer."
    Write-Host "After updating MSVC, delete vcpkg_installed and rerun SetupDependencies.bat."
    exit 1
}

Write-Host "MSVC toolset check passed."
