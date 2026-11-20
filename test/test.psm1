# TcpTestKit.psm1

$global:TcpSessions = @{}

function Connect-TcpServer {
    [CmdletBinding()]
    param (
        [Parameter(Mandatory = $true, Position = 0)]
        [string]$ComputerName,

        [Parameter(Mandatory = $true, Position = 1)]
        [int]$Port,

        [Parameter(Mandatory = $true)]
        [string]$SessionId
    )

    try {
        $client = New-Object System.Net.Sockets.TcpClient($ComputerName, $Port)
        $stream = $client.GetStream()
        $global:TcpSessions[$SessionId] = @{
            Client = $client
            Stream = $stream
            Remote = "{0}:{1}" -f $ComputerName, $Port
        }
    }
    catch {
        Throw $_
    }
}

function Send-TcpData {
    [CmdletBinding()]
    param (
        [Parameter(Mandatory = $true, Position = 0)]
        [string]$SessionId,

        [Parameter(Mandatory = $true, ValueFromPipeline = $true)]
        [object]$InputObject
    )

    begin {
        if (-not $global:TcpSessions.ContainsKey($SessionId)) {
            throw "Session '${SessionId}' not found."
        }
        $buffer = [System.Collections.Generic.List[byte]]::new()
    }

    process {
        if ($null -eq $InputObject) { return }

        if ($InputObject -is [string]) {
            $bytes = [System.Text.Encoding]::UTF8.GetBytes($InputObject)
            $buffer.AddRange($bytes)
        }
        elseif ($InputObject -is [byte[]]) {
            $buffer.AddRange($InputObject)
        }
        elseif ($InputObject.GetType().IsPrimitive -and $InputObject -isnot [bool] -and $InputObject -isnot [char]) {
            $val = [long]$InputObject
            if ($val -lt 0 -or $val -gt 255) {
                throw "Value '${val}' is out of range for a single byte (0-255)."
            }
            $buffer.Add([byte]$val)
        }
        else {
            throw "Unsupported type: $($InputObject.GetType().FullName)"
        }
    }

    end {
        if ($buffer.Count -eq 0) { return }
        
        try {
            $session = $global:TcpSessions[$SessionId]
            $finalBytes = $buffer.ToArray()
            $session.Stream.Write($finalBytes, 0, $finalBytes.Length)
            $session.Stream.Flush()
        }
        catch {
            Throw $_
        }
    }
}

function Receive-TcpData {
    [CmdletBinding()]
    param (
        [Parameter(Mandatory = $true, Position = 0)]
        [string]$SessionId,

        [Parameter()]
        [ValidateSet('Bytes', 'Hex', 'ASCII', 'UTF8')]
        [string]$As = 'UTF8',

        [int]$TimeoutMs = 500
    )

    if (-not $global:TcpSessions.ContainsKey($SessionId)) {
        throw "Session '${SessionId}' not found."
    }

    $session = $global:TcpSessions[$SessionId]
    $stream  = $session.Stream

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while (-not $stream.DataAvailable -and $sw.ElapsedMilliseconds -lt $TimeoutMs) {
        Start-Sleep -Milliseconds 10
    }

    if ($stream.DataAvailable) {
        $readBuffer = New-Object byte[] 65535
        $bytesRead = $stream.Read($readBuffer, 0, $readBuffer.Length)
        $data = $readBuffer[0..($bytesRead - 1)]

        switch ($As) {
            'Bytes' { return $data }
            'Hex'   { return (($data | ForEach-Object { $_.ToString("X2") }) -join " ") }
            default { return ([System.Text.Encoding]::$As).GetString($data) }
        }
    }
    return $null
}

function Get-TcpSession {
    if ($global:TcpSessions.Count -eq 0) { return @() }
    $result = @()
    foreach ($key in $global:TcpSessions.Keys) {
        $session = $global:TcpSessions[$key]
        $result += [PSCustomObject]@{
            SessionId = $key
            Remote    = $session.Remote
            Connected = $session.Client.Connected
        }
    }
    return $result
}

function Disconnect-TcpServer {
    param (
        [Parameter(Position = 0)]
        [string]$SessionId
    )

    $ids = if ($SessionId) { ,$SessionId } else { $global:TcpSessions.Keys | ForEach-Object {$_} }

    foreach ($id in $ids) {
        if ($global:TcpSessions.ContainsKey($id)) {
            $session = $global:TcpSessions[$id]
            if ($session.Stream) { $session.Stream.Close() }
            if ($session.Client) { $session.Client.Close() }
            $global:TcpSessions.Remove($id)
        }
    }
}

Export-ModuleMember -Function Connect-TcpServer, Send-TcpData, Receive-TcpData, Get-TcpSession, Disconnect-TcpServer

