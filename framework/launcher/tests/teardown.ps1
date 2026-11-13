#
# The launcher's promise on Windows, end to end, against the real binary -- what
# teardown.sh is on macOS: a second launcher opens a window and exits, and when
# the launcher dies -- here by TerminateProcess, the case no destructor sees --
# nothing it started is left running, but a run in flight is given the time to
# wind down first. That is src/windows/platform.cpp's Job object and watchdog
# doing their job, and the one thing about this program that matters most if it
# goes wrong: a leaked run_scripts is a rig nobody is watching, and one killed
# where it stands is a rig left energised.
#
# Usage: teardown.ps1 <path to thorium_launcher.exe> <path to stand_in_server.ps1>
#
# Needs a logged-in desktop and Chrome, since it starts the real thing: skipped
# (exit 77) without Chrome, or while a console is really running -- the
# launcher's lock is one per session, and this test must not be the second
# launcher of somebody's bench. It opens one Chrome app-mode window for a few
# seconds, against a throwaway %LOCALAPPDATA%. The "server" is
# stand_in_server.ps1 -- the launcher needs something that answers /api/status,
# and this test is about the process group, not about thorium_webui. It also
# starts a child that takes four seconds to wind down once the server has gone,
# as a run that is safing its rig does, which the watchdog has to wait for
# rather than kill.
#
# A clean exit (the operator closing the window) is not driven from here: it
# needs a real console page holding /api/presence open. What it does to the
# children -- the launcher's process ending -- is what a kill does, tested below.
#
param(
    [Parameter(Mandatory)] [string] $Launcher,
    [Parameter(Mandatory)] [string] $StandIn
)

$ErrorActionPreference = 'Stop'

$Port = 8499
$PowerShell = (Get-Process -Id $PID).Path
$TestHome = $null

function Skip([string] $why) { Write-Output "skipped: $why"; Cleanup; exit 77 }
function Fail([string] $why) { Write-Output "FAIL: $why"; Cleanup; exit 1 }

# Everything started under the throwaway directory, whoever its parent is: the
# stand-ins are started with a path inside it and Chrome with a profile inside
# it, and nothing else on the machine names it.
function Ours {
    if ($null -eq $TestHome) { return @() }
    @(Get-CimInstance Win32_Process |
        Where-Object { $_.ProcessId -ne $PID -and $_.CommandLine -and $_.CommandLine.Contains($TestHome) })
}

function Cleanup {
    foreach ($launcher in @($script:First, $script:Restarted)) {
        if ($launcher -and -not $launcher.HasExited) { Stop-Process -Id $launcher.Id -Force -ErrorAction SilentlyContinue }
    }
    Ours | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    if ($TestHome) {
        # A Chrome that has just been ended still has files of its profile open
        # for a moment.
        foreach ($attempt in 1..20) {
            Remove-Item -LiteralPath $TestHome -Recurse -Force -ErrorAction SilentlyContinue
            if (-not (Test-Path -LiteralPath $TestHome)) { break }
            Start-Sleep -Milliseconds 250
        }
    }
}

function Listening {
    $client = New-Object System.Net.Sockets.TcpClient
    try { $client.Connect('127.0.0.1', $Port); return $true }
    catch { return $false }
    finally { $client.Close() }
}

function Alive([int] $id) { $null -ne (Get-Process -Id $id -ErrorAction SilentlyContinue) }

# Waits, bounded, for the condition; says whether it came.
function WaitFor([int] $seconds, [scriptblock] $condition) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        if (& $condition) { return $true }
        Start-Sleep -Milliseconds 100
    }
    return [bool] (& $condition)
}

function Quoted([string] $text) { '"' + $text + '"' }

function StartLauncher([string] $woundDown) {
    $arguments = @(
        (Quoted "--server=$PowerShell"),
        '--server-arg=-NoProfile', '--server-arg=-ExecutionPolicy', '--server-arg=Bypass', '--server-arg=-File',
        (Quoted "--server-arg=$StandIn"),
        "--server-arg=$Port")
    if ($woundDown) { $arguments += (Quoted "--server-arg=$woundDown") }
    $arguments += "--port=$Port"

    Start-Process -FilePath $Launcher -ArgumentList ($arguments -join ' ') -PassThru
}

$chrome = 'SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\chrome.exe'
if (-not ((Test-Path "HKCU:\$chrome") -or (Test-Path "HKLM:\$chrome"))) { Skip 'Chrome is not installed' }

$held = $null
if ([System.Threading.Mutex]::TryOpenExisting('Local\ThoriumLauncherSingleInstance', [ref] $held)) {
    $held.Dispose()
    Skip 'a bench console is running in this session'
}
if (Listening) { Skip "port $Port is already in use" }

$TestHome = Join-Path ([System.IO.Path]::GetTempPath()) ('thorium-launcher-test.' + [System.IO.Path]::GetRandomFileName())
$null = New-Item -ItemType Directory -Path $TestHome
$WoundDown = Join-Path $TestHome 'wound-down'

# Where the launcher keeps Chrome's profile -- and what Ours finds things by.
$env:LOCALAPPDATA = $TestHome

# --- Start it ---------------------------------------------------------------
$script:First = StartLauncher $WoundDown

if (-not (WaitFor 20 { Listening })) { Fail 'the stand-in server never came up' }

$watchdog = @(Get-CimInstance Win32_Process -Filter "ParentProcessId = $($First.Id)" |
    Where-Object { $_.CommandLine -like '*--thorium-watchdog*' })
if ($watchdog.Count -ne 1) { Fail "expected one watchdog beside the launcher, found $($watchdog.Count)" }
$watchdogId = [int] $watchdog[0].ProcessId

$server = @(Ours | Where-Object { $_.ParentProcessId -eq $First.Id -and $_.CommandLine.Contains($StandIn) })
if ($server.Count -ne 1) { Fail "expected one stand-in server, found $($server.Count)" }
$serverId = [int] $server[0].ProcessId

$run = @()
if (-not (WaitFor 10 { $script:run = @(Get-CimInstance Win32_Process -Filter "ParentProcessId = $serverId" |
                                          Where-Object { $_.Name -like 'powershell*' -or $_.Name -like 'pwsh*' })
                       $script:run.Count -eq 1 })) {
    Fail 'the stand-in run never started'
}
$runId = [int] $run[0].ProcessId

# A renderer, not just a chrome.exe: a Chrome that is still starting does not
# answer the second launcher's Chrome below, which then waits for it -- and
# becomes the browser itself when the first is killed under it.
if (-not (WaitFor 30 { @(Ours | Where-Object { $_.Name -eq 'chrome.exe' -and $_.CommandLine -like '*--type=renderer*' }).Count -gt 0 })) {
    Fail 'Chrome never opened on the console'
}
Write-Output "console is up: launcher $($First.Id), watchdog $watchdogId, server $serverId, run $runId"

# --- A second launcher ------------------------------------------------------
$second = StartLauncher $null
if (-not $second.WaitForExit(15000)) {
    Stop-Process -Id $second.Id -Force -ErrorAction SilentlyContinue
    Fail 'a second launcher did not exit'
}
if ($second.ExitCode -ne 0) { Fail "a second launcher exited $($second.ExitCode)" }
if (-not (Alive $serverId)) { Fail 'a second launcher disturbed the server' }
Write-Output 'second instance opened a window and exited'

# --- Kill the launcher ------------------------------------------------------
$killed = Get-Date
Stop-Process -Id $First.Id -Force
$script:First = $null

# And start the console again at once, as an operator would. The old server is
# gone within moments and its port is free, but the old run is still winding
# down: the new console must not come up beside it.
$script:Restarted = StartLauncher $null

if (-not (WaitFor 5 { -not (Alive $serverId) })) { Fail 'the server outlived the launcher' }
if (-not (Alive $runId) -and -not (Test-Path -LiteralPath $WoundDown)) {
    Fail 'the run in flight was killed with the launcher, before it had wound down'
}

if (-not (WaitFor 30 { Listening })) { Fail 'the restarted console never came up' }
$cameUp = ((Get-Date) - $killed).TotalSeconds

if (-not (Test-Path -LiteralPath $WoundDown)) {
    Fail ('the restarted console came up after {0:N1} s, while the old run was still winding down' -f $cameUp)
}
if ((Alive $runId) -or (Alive $watchdogId)) { Fail 'the restarted console came up beside the old one''s processes' }
Write-Output ('a console restarted at once waited {0:N1} s for the old run to wind down' -f $cameUp)
Write-Output 'killing the launcher took everything it started with it, once the run had wound down'

# --- The restarted console goes the same way, with nothing slow in it --------
# What it started: its watchdog, its server and its Chrome. Not everything on
# the throwaway profile -- the second launcher's Chrome is deliberately outside
# the job, and Cleanup sees to whatever is left of it.
$started = @(Get-CimInstance Win32_Process -Filter "ParentProcessId = $($Restarted.Id)" | ForEach-Object { [int] $_.ProcessId })
if ($started.Count -lt 2) { Fail "the restarted console had started $($started.Count) processes, not a watchdog and a server" }

Stop-Process -Id $Restarted.Id -Force
$script:Restarted = $null

if (-not (WaitFor 15 { @($started | Where-Object { Alive $_ }).Count -eq 0 })) {
    $left = ($started | Where-Object { Alive $_ } | ForEach-Object { "$_ $((Get-Process -Id $_).ProcessName)" }) -join ', '
    Fail "the restarted console left processes behind: $left"
}
if (Listening) { Fail "something is still answering on port $Port" }
Write-Output 'and a console with no run in flight was gone within moments'

Cleanup
exit 0
