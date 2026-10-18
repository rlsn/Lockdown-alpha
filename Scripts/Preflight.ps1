[CmdletBinding()]
param([string]$EngineRoot)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-LDEngine $EngineRoot
$output = New-LDArtifactDirectory 'Preflight'
$version = Get-Content -LiteralPath (Join-Path $engine 'Engine/Build/Build.version') -Raw | ConvertFrom-Json
$sdkRules = Get-Content -LiteralPath (Join-Path $engine 'Engine/Config/Windows/Windows_SDK.json') -Raw | ConvertFrom-Json
$tools = @('Engine/Binaries/Win64/UnrealEditor.exe', 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe', 'Engine/Build/BatchFiles/Build.bat', 'Engine/Build/BatchFiles/RunUAT.bat')
$missing = @($tools | Where-Object { -not (Test-Path -LiteralPath (Join-Path $engine $_)) })
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installations = @()
if (Test-Path -LiteralPath $vswhere) {
    $installations = @((& $vswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json) | ConvertFrom-Json)
}
$compilers = @(foreach ($installation in $installations) {
    $compilerRoot = Join-Path $installation.installationPath 'VC/Tools/MSVC'
    if (-not (Test-Path -LiteralPath $compilerRoot)) { continue }
    foreach ($family in Get-ChildItem -LiteralPath $compilerRoot -Directory) {
        $compiler = Join-Path $family.FullName 'bin/Hostx64/x64/cl.exe'
        if (-not (Test-Path -LiteralPath $compiler)) { continue }
        $rawCompilerVersion = [version](Get-Item -LiteralPath $compiler).VersionInfo.ProductVersion
        $compilerVersion = [version]::new($rawCompilerVersion.Major, $rawCompilerVersion.Minor, $rawCompilerVersion.Build)
        $banned = $false
        foreach ($range in $sdkRules.BannedVisualCppVersions) {
            $bounds = $range.Split('-')
            if ($compilerVersion -ge [version]$bounds[0] -and $compilerVersion -le [version]$bounds[1]) { $banned = $true }
        }
        [pscustomobject]@{Path=$compiler;Version=$compilerVersion.ToString();Banned=$banned;MeetsMinimum=($compilerVersion -ge [version]$sdkRules.MinimumVisualCppVersion)}
    }
})
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdks = @(Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory -ErrorAction SilentlyContinue | Where-Object {
    (Test-Path -LiteralPath (Join-Path $_.FullName 'um/Windows.h')) -and
    (Test-Path -LiteralPath (Join-Path $sdkRoot "Lib/$($_.Name)/um/x64/kernel32.lib")) -and
    (Test-Path -LiteralPath (Join-Path $sdkRoot "bin/$($_.Name)/x64/rc.exe"))
} | Select-Object -ExpandProperty Name)
$report = [ordered]@{
    timestamp = (Get-Date -Format o)
    engineRoot = $engine
    engineVersion = $version
    installedBuild = (Test-Path -LiteralPath (Join-Path $engine 'Engine/Build/InstalledBuild.txt'))
    project = Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
    os = Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber, TotalVisibleMemorySize
    cpu = Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors
    gpu = @(Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion, AdapterRAM)
    disk = Get-Volume -FilePath (Get-LDProjectRoot) | Select-Object SizeRemaining, Size
    compilers = $compilers
    windowsSdks = $sdks
    missingEngineTools = $missing
    buildVerified = $false
}
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $output 'Environment.json') -Encoding utf8
Write-Host "Preflight evidence: $output"
$compilers | Format-Table Version,Banned,MeetsMinimum,Path -AutoSize
if ($missing.Count -gt 0) { throw "Missing engine tools: $($missing -join ', ')" }
if (@($compilers | Where-Object { -not $_.Banned -and $_.MeetsMinimum }).Count -eq 0) {
    throw 'No supported MSVC compiler found. Use the pinned engine Windows_SDK.json rules; preferred 14.50 requires 14.50.35723 or newer. Update through Visual Studio Installer, then rerun.'
}
if (@($sdks | Where-Object { [version]$_ -ge [version]$sdkRules.MinVersion -and [version]$_ -le [version]$sdkRules.MaxVersion }).Count -eq 0) { throw 'No complete supported Windows SDK found.' }
Write-Host 'Discovery passed. A cold Editor build is still required.'
