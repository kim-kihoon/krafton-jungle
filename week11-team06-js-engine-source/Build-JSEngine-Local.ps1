param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectPath = Join-Path $ProjectRoot "JSEngine\JSEngine.vcxproj"
$MSBuildPath = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"

if (-not (Test-Path $MSBuildPath)) {
    throw "MSBuild was not found: $MSBuildPath"
}

& $MSBuildPath `
    $ProjectPath `
    "/p:Configuration=$Configuration" `
    "/p:Platform=$Platform" `
    "/p:PlatformToolset=v145" `
    "/m"

exit $LASTEXITCODE
