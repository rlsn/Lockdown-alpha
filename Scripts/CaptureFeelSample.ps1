[CmdletBinding()]
param([string]$EngineRoot,[string]$FFmpeg,[ValidateRange(1280,2560)][int]$Width=1600,[ValidateRange(720,1440)][int]$Height=900,[ValidateRange(60,900)][int]$TimeoutSeconds=300)
# Audiovisual game-feel sample (docs/systems/AUDIO_AND_FEEL.md): the combat proof stage plays the "feel" scenario at real game
# speed with the audio device on. The game records its own master submix (game_audio.wav); ffmpeg records the game window;
# both are muxed on their logged wall clocks into feel.mp4. A DEV fixture on the real presentation components, not a match.
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
Set-LDPlatformValidation Skip
if(-not $FFmpeg){$FFmpeg=(Get-Command ffmpeg -ErrorAction SilentlyContinue).Source}
if(-not $FFmpeg -or -not(Test-Path -LiteralPath $FFmpeg)){throw 'ffmpeg is required: pass -FFmpeg <path to ffmpeg.exe>.'}
$binary=Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$output=New-LDArtifactDirectory ('FeelSample-'+$Width+'x'+$Height)
$user=Join-Path $output 'User'
New-Item -ItemType Directory -Path $user | Out-Null
$world=(Get-LDDemoMaps)[1]+'?listen'
$log=Join-Path $output 'Art.log'
$arguments='"{0}" {1} -game -windowed -ForceRes -ResX={2} -ResY={3} -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -UserDir="{4}" -LDDemoArtCapture -LDDemoArtCaptureOutput="{5}" -abslog="{6}" -LDDemoCombatVfxCapture -LDDemoVfxProof=feel -LDDemoVfxProofLabel=feel -LDAudioEventLog' -f $project,$world,$Width,$Height,$user,$output,$log
$process=Start-Process -FilePath $binary -ArgumentList $arguments -PassThru
$recorder=$null
try {
    # The window appears once the map loads; record it from then on.
    $deadline=(Get-Date).AddSeconds($TimeoutSeconds)
    $title=$null
    while(-not $title -and (Get-Date) -lt $deadline -and -not $process.HasExited){
        Start-Sleep -Milliseconds 500
        $process.Refresh()
        if($process.MainWindowTitle){$title=$process.MainWindowTitle}
    }
    if(-not $title){throw "The game window never appeared: $output"}
    $video=Join-Path $output 'window.mkv'
    $recordStart=(Get-Date).ToUniversalTime().ToString('o')
    $recorder=Start-Process -FilePath $FFmpeg -ArgumentList @('-hide_banner','-loglevel','error','-y','-f','gdigrab','-framerate','30','-draw_mouse','0','-i',('title='+$title),'-c:v','libx264','-preset','ultrafast','-pix_fmt','yuv420p',$video) -PassThru -WindowStyle Hidden
    if(-not $process.WaitForExit([int](($deadline-(Get-Date)).TotalMilliseconds))){throw "Feel capture timed out: $output"}
    Start-Sleep -Milliseconds 300
    if(-not $recorder.HasExited){Stop-Process -Id $recorder.Id; $recorder.WaitForExit()}
    $start=Select-String -LiteralPath $log -Pattern 'LD_FEEL_RECORDING_START utc=(\S+)' | Select-Object -First 1
    $wav=Get-ChildItem -LiteralPath $output -Recurse -Filter 'game_audio.wav' | Select-Object -First 1
    if(-not $start -or -not $wav){throw "The game did not record its audio: $log"}
    $offset=([datetime]::Parse($start.Matches[0].Groups[1].Value).ToUniversalTime()-[datetime]::Parse($recordStart).ToUniversalTime()).TotalSeconds
    @{window_title=$title;window_record_start_utc=$recordStart;game_audio=$wav.FullName;audio_offset_seconds=$offset;sourceRevision=(git rev-parse HEAD);sourceDirty=[bool](git status --porcelain);display=(Get-LDDisplayEnvironment)}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $output 'Feel.json') -Encoding utf8
    Write-Host "Feel sample recorded (audio offset $([math]::Round($offset,3)) s): $output"
} finally {
    if($recorder -and -not $recorder.HasExited){Stop-Process -Id $recorder.Id}
    if(-not $process.HasExited){Stop-Process -Id $process.Id;$process.WaitForExit()}
}
