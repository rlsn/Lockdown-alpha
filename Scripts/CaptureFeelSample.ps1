[CmdletBinding()]
param([string]$EngineRoot,[string]$FFmpeg,[ValidateRange(1280,2560)][int]$Width=1600,[ValidateRange(720,1440)][int]$Height=900,[ValidateRange(60,900)][int]$TimeoutSeconds=300,[ValidateSet('feel','reel','combat')][string]$Scenario='feel',[ValidateSet('720p','1080p')][string]$CombatResolution='720p')
# Audiovisual game-feel sample (docs/systems/AUDIO_AND_FEEL.md). The game records itself (Probes/LDGameRecorder.h): its own
# frames from the game viewport and its own master submix. Nothing reads the desktop or the system audio, so the game runs
# off screen and the user's windows, video or sound never reach the footage. feel/reel: the combat proof stage plays the
# scenario at real game speed; MakeFeelEvidence.py places the frames and the audio on the game's event
# clock into feel.mp4. combat: a real owned fight (RunDemoCombatCheck -CaptureOnly), muxed into combat.mp4.
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
Set-LDPlatformValidation Skip
if(-not $FFmpeg){$FFmpeg=(Get-Command ffmpeg -ErrorAction SilentlyContinue).Source}
if(-not $FFmpeg -or -not(Test-Path -LiteralPath $FFmpeg)){throw 'ffmpeg is required: pass -FFmpeg <path to ffmpeg.exe>.'}
# The -Cmd binary keeps the recorder's per-app speaker mute away from the interactive editor's mixer entry.
$binary=Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$output=New-LDArtifactDirectory ('FeelSample-'+$Scenario+'-'+$Width+'x'+$Height)
$game=Join-Path $output 'game'
if($Scenario -eq 'combat'){
    # The check's own report and screenshots stay in its evidence folder; it prints no verdict in capture-only mode.
    $check=Start-Process -FilePath pwsh -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $PSScriptRoot 'RunDemoCombatCheck.ps1')+'"'),'-Resolution',$CombatResolution,'-CaptureOnly','-RecordGame',('"'+$game+'"')) -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $output 'check.log') -RedirectStandardError (Join-Path $output 'check.err')
    if(-not $check.WaitForExit($TimeoutSeconds*1000+180000)){Stop-Process -Id $check.Id; throw "The combat capture timed out: $output"}
    if(-not(Test-Path -LiteralPath (Join-Path $game 'recorder.json'))){throw "The Host did not record itself: $(Join-Path $output 'check.log')"}
    & python -B (Join-Path $PSScriptRoot 'AssembleGameRecording.py') $game --ffmpeg $FFmpeg --out (Join-Path $output 'combat.mp4')
    if($LASTEXITCODE -ne 0){throw "The recording could not be assembled: $game"}
    Write-Host "Combat recorded from the game itself: $(Join-Path $output 'combat.mp4')"
    Write-Host (((Get-Content -LiteralPath (Join-Path $output 'check.log') -Raw) -split "`n" | Where-Object { $_ -match 'CAPTURE ONLY|Evidence' } | Select-Object -Last 2) -join "`n")
    return
}
$user=Join-Path $output 'User'
New-Item -ItemType Directory -Path $user | Out-Null
$world=(Get-LDDemoMaps)[1]+'?listen'
$log=Join-Path $output 'Art.log'
$arguments='"{0}" {1} -game -windowed -RenderOffScreen -ForceRes -ResX={2} -ResY={3} -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -UserDir="{4}" -LDDemoArtCapture -LDDemoArtCaptureOutput="{5}" -abslog="{6}" -LDDemoCombatVfxCapture -LDDemoVfxProof={7} -LDDemoVfxProofLabel=feel -LDAudioEventLog -LDRecordGame="{8}"' -f $project,$world,$Width,$Height,$user,$output,$log,$(if($Scenario -eq 'reel'){'feel,reel'}else{'feel'}),$game
$process=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "Feel capture timed out: $output"}
    $start=Select-String -LiteralPath $log -Pattern 'LD_FEEL_RECORDING_START utc=(\S+)' | Select-Object -First 1
    if(-not $start){throw "The feel scenario did not start: $log"}
    if(-not(Test-Path -LiteralPath (Join-Path $game 'game_audio.wav'))){throw "The game did not record its audio: $log"}
    & python -B (Join-Path $PSScriptRoot 'AssembleGameRecording.py') $game --ffmpeg $FFmpeg --out (Join-Path $output 'game.mp4')
    if($LASTEXITCODE -ne 0){throw "The recording could not be assembled: $game"}
    @{scenario_start_utc=$start.Matches[0].Groups[1].Value;recording=$game;video=(Join-Path $output 'game.mp4');sourceRevision=(git rev-parse HEAD);sourceDirty=[bool](git status --porcelain);display=(Get-LDDisplayEnvironment)}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $output 'Feel.json') -Encoding utf8
    Write-Host "Feel sample recorded from the game itself: $output"
} finally {
    if(-not $process.HasExited){Stop-Process -Id $process.Id;$process.WaitForExit()}
}
