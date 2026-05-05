$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$SourceRoot = Join-Path $Root "NipsEngine\Source"
$CheckRoots = @(
    (Join-Path $SourceRoot "Game")
)

$IncludePattern = '^\s*#\s*include\s*[<"](?:Source/)?Editor[/\\]'
$SourceExtensions = @(".h", ".hpp", ".hxx", ".inl", ".cpp", ".c", ".cc", ".cxx")
$Violations = New-Object System.Collections.Generic.List[string]

foreach ($CheckRoot in $CheckRoots) {
    if (-not (Test-Path $CheckRoot)) {
        continue
    }

    Get-ChildItem -Path $CheckRoot -Recurse -File | Where-Object {
        $SourceExtensions -contains $_.Extension.ToLowerInvariant()
    } | ForEach-Object {
        $Path = $_.FullName
        $LineNumber = 0
        Get-Content -LiteralPath $Path | ForEach-Object {
            $LineNumber++
            if ($_ -match $IncludePattern) {
                $Relative = [System.IO.Path]::GetRelativePath($Root, $Path)
                $Violations.Add("${Relative}:${LineNumber}: $($_.Trim())")
            }
        }
    }
}

if ($Violations.Count -eq 0) {
    Write-Host "Dependency boundary check passed."
    exit 0
}

Write-Host "Dependency boundary check failed:"
foreach ($Violation in $Violations) {
    Write-Host "  $Violation"
}
exit 1
