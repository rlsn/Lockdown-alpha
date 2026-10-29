[CmdletBinding()]
param([string]$EngineRoot,[switch]$Capture,[switch]$Objects,[switch]$Tools,[switch]$Traversal,[switch]$Furniture,[switch]$Saplings,[switch]$Fields,[switch]$Ignition,[switch]$Grass,[ValidateSet('A','B')][string]$RenderedClient='A',[ValidateRange(1024,65000)][int]$Port=7911,[ValidateRange(60,900)][int]$TimeoutSeconds=300)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
if($Furniture){if($Objects -or $Tools -or $Traversal -or $Saplings -or $Fields -or $Ignition -or $Grass){throw 'Choose one native scenario'};$Objects=$true}
if(([int]$Objects.IsPresent+[int]$Tools.IsPresent+[int]$Traversal.IsPresent+[int]$Saplings.IsPresent+[int]$Fields.IsPresent+[int]$Ignition.IsPresent+[int]$Grass.IsPresent) -gt 1){throw 'Choose one native scenario'}
& python -B (Join-Path $PSScriptRoot 'SyncB91Grass.py') --check
if($LASTEXITCODE -ne 0){throw 'B91 grass contract is stale'}
& python -B (Join-Path $PSScriptRoot 'SyncB91Forestry.py') --check
if($LASTEXITCODE -ne 0){throw 'B91 forestry/control contract is stale'}
& python -B (Join-Path $PSScriptRoot 'SyncB91Traversal.py') --check
if($LASTEXITCODE -ne 0){throw 'B91 traversal contract is stale'}
if($Objects){
    & python -B (Join-Path $PSScriptRoot 'SyncB91Objects.py') --check
    if($LASTEXITCODE -ne 0){throw 'B91 objects contract is stale'}
}
if($Objects -or $Tools){
    & python -B (Join-Path $PSScriptRoot 'SyncB91Tools.py') --check
    if($LASTEXITCODE -ne 0){throw 'B91 tools contract is stale'}
}
$output=New-LDArtifactDirectory $(if($Grass){'B91Grass-Network'}elseif($Ignition){'B91Ignition-Network'}elseif($Fields){'B91Fields-Network'}elseif($Saplings){'B91Saplings-Network'}elseif($Furniture){'B91Furniture-Network'}elseif($Traversal){'B91Traversal-Network'}elseif($Tools){'B91Tools-Network'}elseif($Objects){'B91Objects-Network'}elseif($Capture){"B91Network-Capture-$RenderedClient"}else{'B91Network'})
$owned=@()
try {
    foreach($role in @('Server','A','B')){
        $destination=if($role -eq 'Server'){'/Game/Lockdown/Generated/V09/L_V09Full?game=/Script/Lockdown.LDB91GameMode?Profile=v09_environment'}else{"127.0.0.1:$Port"}
        $mode=if($role -eq 'Server'){'-server'}else{'-game'}
        $surface=if($Capture -and $role -eq $RenderedClient){'-RenderOffScreen -windowed -ForceRes -ResX=1280 -ResY=720 -LDB91NetCapture'}else{'-nullrhi'}
        $commands=if($Capture -and $role -eq $RenderedClient){'t.MaxFPS 30,sg.ShadowQuality 1,r.Shadow.Virtual.MaxPhysicalPages 512,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0'}else{'t.MaxFPS 60'}
        $arguments='"{0}" {1} {2} {3} -port={4} -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -ExecCmds="{5}" -LDB91NetProbe -B91Role={6} -B91Output="{7}" -abslog="{8}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$destination,$mode,$surface,$Port,$commands,$role,$output,(Join-Path $output "$role.log")
        if($Objects){$arguments+=' -LDB91ObjectsProbe'}
        if($Furniture){$arguments+=' -LDB91FurnitureProbe'}
        if($Tools){$arguments+=' -LDB91ToolsProbe'}
        if($Traversal){$arguments+=' -LDB91TraversalProbe'}
        if($Saplings){$arguments+=' -LDB91SaplingsProbe'}
        if($Fields){$arguments+=' -LDB91FieldsProbe'}
        if($Ignition){$arguments+=' -LDB91IgnitionProbe'}
        if($Grass){$arguments+=' -LDB91GrassProbe'}
        $owned+=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if($role -eq 'Server'){Start-Sleep -Seconds 3}
    }
    $deadline=(Get-Date).AddSeconds($TimeoutSeconds)
    while((Get-Date) -lt $deadline -and -not $owned[0].HasExited){
        foreach($client in @($owned | Select-Object -Skip 1)){if($client.HasExited){throw "B91 client exited before scenario completion: $output"}}
        Start-Sleep -Milliseconds 500
    }
    if(-not $owned[0].HasExited){throw "B91 network fixture timed out: $output"}
    & python -B (Join-Path $PSScriptRoot $(if($Grass){'CheckB91Grass.py'}elseif($Ignition){'CheckB91Ignition.py'}elseif($Fields){'CheckB91Fields.py'}elseif($Saplings){'CheckB91Saplings.py'}elseif($Traversal){'CheckB91Traversal.py'}elseif($Tools){'CheckB91Tools.py'}elseif($Objects){'CheckB91Objects.py'}else{'CheckB91Network.py'})) $output
    if($LASTEXITCODE -ne 0){throw "B91 network evidence rejected: $output"}
    Write-Host "B91 network evidence: $output"
} finally {foreach($process in $owned){if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}}
