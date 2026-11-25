[CmdletBinding()]
param([string]$EngineRoot,[string]$FFmpeg,[ValidateRange(1280,2560)][int]$Width=1600,[ValidateRange(720,1440)][int]$Height=900,[ValidateRange(60,900)][int]$TimeoutSeconds=300,[ValidateSet('feel','reel','combat')][string]$Scenario='feel',[ValidateSet('720p','1080p')][string]$CombatResolution='720p')
# Audiovisual game-feel sample (docs/systems/AUDIO_AND_FEEL.md): the combat proof stage plays the "feel" scenario at real game
# speed with the audio device on. The game records its own master submix (game_audio.wav); ffmpeg records the game window;
# the speaker output is recorded by WASAPI loopback (Scripts/RecordAudioLoopback.py); MakeFeelEvidence.py muxes the window
# video with the loopback audio on their wall clocks into feel.mp4. A DEV fixture on the real presentation components, not a match.
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
$output=New-LDArtifactDirectory ('FeelSample-'+$Scenario+'-'+$Width+'x'+$Height)
if($Scenario -eq 'combat'){
    # A real owned fight (RunDemoCombatCheck: Host and Client1 attack, cast, die and respawn) at 1080p: the Host window and
    # the speaker output are recorded for the whole check and muxed on their UTC clocks into combat.mp4. The check's own
    # verdict and screenshots stay in its evidence folder.
    $stopFlag=Join-Path $output 'loopback.stop'
    $listener=Start-Process -FilePath python -ArgumentList @('-B',('"'+(Join-Path $PSScriptRoot 'RecordAudioLoopback.py')+'"'),'--out',('"'+(Join-Path $output 'loopback.wav')+'"'),'--stop',('"'+$stopFlag+'"')) -PassThru -WindowStyle Hidden -RedirectStandardError (Join-Path $output 'loopback.log')
    $listener.PriorityClass='High'
    $check=Start-Process -FilePath pwsh -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $PSScriptRoot 'RunDemoCombatCheck.ps1')+'"'),'-Resolution',$CombatResolution,'-CaptureOnly') -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $output 'check.log') -RedirectStandardError (Join-Path $output 'check.err')
    Add-Type -Namespace LDFeel -Name Win -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
public delegate bool EnumProc(IntPtr h, IntPtr l);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
public struct RECT { public int Left, Top, Right, Bottom; }
public struct POINT { public int X, Y; }
public static IntPtr LargestWindowOf(uint pid) {
    IntPtr best = IntPtr.Zero; int area = 0;
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid) { RECT r; GetClientRect(h, out r);
        int a = (r.Right - r.Left) * (r.Bottom - r.Top); if (a > area) { area = a; best = h; } } return true; }, IntPtr.Zero);
    return best; }
'@
    [LDFeel.Win]::SetProcessDPIAware() | Out-Null
    # The harness starts every role hidden: find the Host by its command line and show its game window.
    $deadline=(Get-Date).AddSeconds($TimeoutSeconds); $host_=$null; $window=[IntPtr]::Zero
    while($window -eq [IntPtr]::Zero -and (Get-Date) -lt $deadline -and -not $check.HasExited){
        Start-Sleep -Milliseconds 500
        $found=Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%'" | Where-Object { $_.CommandLine -like '*DemoCombatHost*' } | Select-Object -First 1
        if($found){ $host_=Get-Process -Id $found.ProcessId -ErrorAction SilentlyContinue; $window=[LDFeel.Win]::LargestWindowOf([uint32]$found.ProcessId)
            Add-Content -LiteralPath (Join-Path $output 'window-search.log') -Value ("{0} pid={1} hwnd={2}" -f (Get-Date -Format o),$found.ProcessId,$window) }
        # The game window, not the splash: it carries the game title once the map is up.
        if($host_){ $host_.Refresh(); $window=if($host_.MainWindowTitle -like 'Lockdown*'){ $host_.MainWindowHandle }else{ [IntPtr]::Zero } }
    }
    if($window -eq [IntPtr]::Zero){ New-Item -ItemType File -Path $stopFlag | Out-Null; throw "The combat Host window never appeared: $output" }
    [LDFeel.Win]::ShowWindow($window,1) | Out-Null; [LDFeel.Win]::SetForegroundWindow($window) | Out-Null
    Start-Sleep -Seconds 2
    $client=New-Object LDFeel.Win+RECT; [LDFeel.Win]::GetClientRect($window,[ref]$client) | Out-Null
    $origin=New-Object LDFeel.Win+POINT; [LDFeel.Win]::ClientToScreen($window,[ref]$origin) | Out-Null
    $video=Join-Path $output 'window.mkv'
    $recordStart=(Get-Date).ToUniversalTime().ToString('o')
    $recorder=Start-Process -FilePath $FFmpeg -ArgumentList @('-hide_banner','-loglevel','error','-y','-f','lavfi','-i',('ddagrab=output_idx=0:framerate=30:offset_x={0}:offset_y={1}:video_size={2}x{3}' -f $origin.X,$origin.Y,($client.Right-$client.Left),($client.Bottom-$client.Top)),'-vf','hwdownload,format=bgra,scale=1600:-2','-fps_mode','passthrough','-c:v','libx264','-preset','ultrafast','-pix_fmt','yuv420p',('"'+$video+'"')) -PassThru -WindowStyle Hidden -RedirectStandardError (Join-Path $output 'ffmpeg.log')
    $host_.WaitForExit([int](($deadline-(Get-Date)).TotalMilliseconds)) | Out-Null
    if(-not $recorder.HasExited){Stop-Process -Id $recorder.Id; $recorder.WaitForExit()}
    New-Item -ItemType File -Path $stopFlag | Out-Null
    $listener.WaitForExit(20000) | Out-Null
    $check.WaitForExit(120000) | Out-Null
    $loop=Get-Content -LiteralPath (Join-Path $output 'loopback.json') -Raw | ConvertFrom-Json
    $offset=([datetime]::Parse($recordStart).ToUniversalTime()-[datetime]::Parse($loop.start_utc).ToUniversalTime()).TotalSeconds
    & $FFmpeg -hide_banner -loglevel error -y -i $video -ss ([math]::Max($offset,0)).ToString('0.000',[cultureinfo]::InvariantCulture) -i (Join-Path $output 'loopback.wav') -map 0:v -map 1:a -vf fps=30 -c:v libx264 -preset medium -crf 22 -pix_fmt yuv420p -c:a aac -b:a 192k -shortest (Join-Path $output 'combat.mp4')
    $verdict=(Get-Content -LiteralPath (Join-Path $output 'check.log') -Raw) -split "`n" | Where-Object { $_ -match 'passed|failed|Evidence' } | Select-Object -Last 3
    Write-Host "Combat feel recorded (loopback offset $([math]::Round($offset,3)) s): $output"
    Write-Host ($verdict -join "`n")
    return
}
$user=Join-Path $output 'User'
New-Item -ItemType Directory -Path $user | Out-Null
$world=(Get-LDDemoMaps)[1]+'?listen'
$log=Join-Path $output 'Art.log'
$arguments='"{0}" {1} -game -windowed -ForceRes -ResX={2} -ResY={3} -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -UserDir="{4}" -LDDemoArtCapture -LDDemoArtCaptureOutput="{5}" -abslog="{6}" -LDDemoCombatVfxCapture -LDDemoVfxProof={7} -LDDemoVfxProofLabel=feel -LDAudioEventLog' -f $project,$world,$Width,$Height,$user,$output,$log,$(if($Scenario -eq 'reel'){'feel,reel'}else{'feel'})
# The device output itself (WASAPI loopback): what a player hears, with the real output latency. It starts before the game so
# the game's heavy start-up cannot make it fall behind, at high priority.
$stopFlag=Join-Path $output 'loopback.stop'
$listener=Start-Process -FilePath python -ArgumentList @('-B',('"'+(Join-Path $PSScriptRoot 'RecordAudioLoopback.py')+'"'),'--out',('"'+(Join-Path $output 'loopback.wav')+'"'),'--stop',('"'+$stopFlag+'"')) -PassThru -WindowStyle Hidden -RedirectStandardError (Join-Path $output 'loopback.log')
$listener.PriorityClass='High'
Start-Sleep -Seconds 2
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
    # Desktop Duplication (ddagrab) reads the flip-model D3D12 window that gdigrab cannot. Only the window's client rectangle
    # (physical pixels) is grabbed and the real frame times are kept, so a busy encoder drops frames instead of squeezing time.
    Add-Type -Namespace LDFeel -Name Win -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
public struct RECT { public int Left, Top, Right, Bottom; }
public struct POINT { public int X, Y; }
'@
    [LDFeel.Win]::SetProcessDPIAware() | Out-Null
    $client=New-Object LDFeel.Win+RECT
    [LDFeel.Win]::GetClientRect($process.MainWindowHandle,[ref]$client) | Out-Null
    $origin=New-Object LDFeel.Win+POINT
    [LDFeel.Win]::ClientToScreen($process.MainWindowHandle,[ref]$origin) | Out-Null
    $rect=@{x=$origin.X;y=$origin.Y;w=$client.Right-$client.Left;h=$client.Bottom-$client.Top}
    $recorder=Start-Process -FilePath $FFmpeg -ArgumentList @('-hide_banner','-loglevel','error','-y','-f','lavfi','-i',('ddagrab=output_idx=0:framerate=30:offset_x={0}:offset_y={1}:video_size={2}x{3}' -f $rect.x,$rect.y,$rect.w,$rect.h),'-vf','hwdownload,format=bgra,scale=1600:-2','-fps_mode','passthrough','-c:v','libx264','-preset','ultrafast','-pix_fmt','yuv420p',('"'+$video+'"')) -PassThru -WindowStyle Hidden -RedirectStandardError (Join-Path $output 'ffmpeg.log')
    if(-not $process.WaitForExit([int](($deadline-(Get-Date)).TotalMilliseconds))){throw "Feel capture timed out: $output"}
    Start-Sleep -Milliseconds 300
    if(-not $recorder.HasExited){Stop-Process -Id $recorder.Id; $recorder.WaitForExit()}
    New-Item -ItemType File -Path $stopFlag | Out-Null
    if(-not $listener.WaitForExit(20000)){Stop-Process -Id $listener.Id; throw "The loopback recorder did not stop: $output"}
    $loop=Get-Content -LiteralPath (Join-Path $output 'loopback.json') -Raw | ConvertFrom-Json
    $start=Select-String -LiteralPath $log -Pattern 'LD_FEEL_RECORDING_START utc=(\S+)' | Select-Object -First 1
    $wav=Get-ChildItem -LiteralPath $output -Recurse -Filter 'game_audio.wav' | Select-Object -First 1
    if(-not $start -or -not $wav){throw "The game did not record its audio: $log"}
    if(-not(Test-Path -LiteralPath $video) -or (Get-Item -LiteralPath $video).Length -eq 0){throw "The window was not recorded: $(Join-Path $output 'ffmpeg.log')"}
    $offset=([datetime]::Parse($start.Matches[0].Groups[1].Value).ToUniversalTime()-[datetime]::Parse($recordStart).ToUniversalTime()).TotalSeconds
    @{window_title=$title;window_rect=$rect;loopback_start_utc=$loop.start_utc;loopback_offset_seconds=([datetime]::Parse($loop.start_utc).ToUniversalTime()-[datetime]::Parse($recordStart).ToUniversalTime()).TotalSeconds;window_record_start_utc=$recordStart;game_audio=$wav.FullName;audio_offset_seconds=$offset;sourceRevision=(git rev-parse HEAD);sourceDirty=[bool](git status --porcelain);display=(Get-LDDisplayEnvironment)}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $output 'Feel.json') -Encoding utf8
    Write-Host "Feel sample recorded (audio offset $([math]::Round($offset,3)) s): $output"
} finally {
    if($recorder -and -not $recorder.HasExited){Stop-Process -Id $recorder.Id}
    if($listener -and -not $listener.HasExited){Stop-Process -Id $listener.Id}
    if(-not $process.HasExited){Stop-Process -Id $process.Id;$process.WaitForExit()}
}
