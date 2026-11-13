#
# The Windows stand-in for thorium_webui in teardown.ps1, as stand_in_server.py
# is for teardown.sh: it answers GET /api/status the way the real server does --
# as a console with no page open and no run in flight. Nothing else about the
# console is imitated.
#
# With a second argument it also stands in for a run in flight: a child that
# takes the end of its stdin the way run_scripts does under --stop-on-stdin --
# as the console having gone, and a request to wind down, which takes it a few
# seconds -- and writes that file once it has. teardown.ps1 uses it to see that
# the launcher's watchdog lets a run finish safing rather than killing it part
# way.
#
# PowerShell rather than Python because every Windows has one.
#
param(
    [Parameter(Mandatory)] [int] $Port,
    [string] $WoundDown = ''
)

if ($WoundDown -ne '') {
    $slowToStop = "[Console]::In.ReadToEnd() | Out-Null; Start-Sleep -Seconds 4; " +
                  "Set-Content -LiteralPath '$($WoundDown -replace "'", "''")' -Value 'wound down'"

    $run = New-Object System.Diagnostics.ProcessStartInfo
    $run.FileName = (Get-Process -Id $PID).Path
    $run.Arguments = '-NoProfile -EncodedCommand ' +
                     [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($slowToStop))
    $run.UseShellExecute = $false
    $run.CreateNoWindow = $true

    # The pipe whose other end this process holds for as long as it lives, and
    # not a moment longer.
    $run.RedirectStandardInput = $true

    $script:child = [System.Diagnostics.Process]::Start($run)
}

$body = '{"viewers":0,"running":false}'
$reply = [Text.Encoding]::ASCII.GetBytes(
    "HTTP/1.1 200 OK`r`nContent-Type: application/json`r`nContent-Length: $($body.Length)`r`n" +
    "Connection: close`r`n`r`n$body")

$listener = New-Object System.Net.Sockets.TcpListener ([System.Net.IPAddress]::Loopback, $Port)
$listener.Start()

while ($true) {
    $client = $listener.AcceptTcpClient()
    try {
        $stream = $client.GetStream()
        $stream.ReadTimeout = 2000

        # One request, of which only its having arrived matters.
        $buffer = New-Object byte[] 4096
        $null = $stream.Read($buffer, 0, $buffer.Length)

        $stream.Write($reply, 0, $reply.Length)
        $stream.Flush()
    }
    catch {
    }
    finally {
        $client.Close()
    }
}
