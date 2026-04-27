param(
    [string]$Port = "COM5",
    [ValidateRange(1, 127)]
    [int]$NodeId = 1,
    [ValidateRange(-5000, 5000)]
    [int]$SpeedRpm = 300,
    [ValidateRange(0.1, 60)]
    [double]$DurationSeconds = 2,
    [ValidateRange(0, 12)]
    [int]$ControlMode = 10,
    [ValidateSet(0, 1, 2)]
    [int]$StopMode = 0,
    [switch]$Run,
    [switch]$StopOnly,
    [switch]$ReadPosition
)

$ErrorActionPreference = "Stop"
$script:LastTxTickMs = 0

function Write-TraceLine {
    param(
        [string]$Message,
        [ValidateSet("INFO", "WARN", "ERROR")]
        [string]$Level = "INFO"
    )

    $timestamp = Get-Date -Format "HH:mm:ss.fff"
    Write-Host ("[{0}] [{1}] {2}" -f $timestamp, $Level, $Message)
}

function ConvertTo-Unsigned16 {
    param([int]$Value)

    if ($Value -lt 0) {
        return [uint16]([int64]$Value + 0x10000)
    }

    return [uint16]$Value
}

function New-Write16Data {
    param(
        [int]$Register,
        [int]$Value
    )

    $index = 0x4000 + $Register
    $raw = ConvertTo-Unsigned16 $Value

    return [byte[]]@(
        0x22,
        ($index -band 0xFF),
        (($index -shr 8) -band 0xFF),
        0x00,
        ($raw -band 0xFF),
        (($raw -shr 8) -band 0xFF),
        0x00,
        0x00
    )
}

function New-Read16Data {
    param([int]$Register)

    $index = 0x4000 + $Register

    return [byte[]]@(
        0x40,
        ($index -band 0xFF),
        (($index -shr 8) -band 0xFF),
        0x00,
        0x00,
        0x00,
        0x00,
        0x00
    )
}

function ConvertTo-HexString {
    param([byte[]]$Data)
    return (($Data | ForEach-Object { $_.ToString("X2") }) -join "")
}

function ConvertTo-PrettyHex {
    param([byte[]]$Data)
    return (($Data | ForEach-Object { $_.ToString("X2") }) -join " ")
}

function New-SlcanFrame {
    param(
        [int]$CanId,
        [byte[]]$Data
    )

    return ("t{0:X3}{1:X1}{2}" -f $CanId, $Data.Length, (ConvertTo-HexString $Data))
}

function Format-Reply {
    param([string]$Reply)

    if (-not $Reply) {
        return ""
    }

    return ($Reply.Replace("`r", "\r").Replace("`n", "\n"))
}

function Wait-TxGap {
    param([int]$GapMs = 10)

    if (-not $Run) {
        return
    }

    $now = [Environment]::TickCount64
    $waitMs = $GapMs - ($now - $script:LastTxTickMs)
    if ($waitMs -gt 0) {
        Start-Sleep -Milliseconds ([int]$waitMs)
    }
}

function Get-CompleteSerialLines {
    param([ref]$Buffer)

    $lines = New-Object System.Collections.Generic.List[string]

    while ($true) {
        $match = [regex]::Match($Buffer.Value, "^(.*?)(\r\n|\n|\r)")
        if (-not $match.Success) {
            break
        }

        $line = $match.Groups[1].Value.Trim()
        $Buffer.Value = $Buffer.Value.Substring($match.Length)
        if (-not [string]::IsNullOrWhiteSpace($line)) {
            $lines.Add($line)
        }
    }

    return @($lines.ToArray())
}

function Parse-SlcanLine {
    param([string]$Line)

    if ([string]::IsNullOrWhiteSpace($Line)) {
        return $null
    }

    $trimmed = $Line.Trim()
    if (($trimmed.Length -lt 5) -or (-not $trimmed.StartsWith("t"))) {
        return $null
    }

    try {
        $canId = [Convert]::ToInt32($trimmed.Substring(1, 3), 16)
        $dlc = [Convert]::ToInt32($trimmed.Substring(4, 1), 16)
        $hexPayload = $trimmed.Substring(5)
        if ($hexPayload.Length -lt ($dlc * 2)) {
            return $null
        }

        $data = New-Object byte[] $dlc
        for ($i = 0; $i -lt $dlc; $i++) {
            $data[$i] = [Convert]::ToByte($hexPayload.Substring($i * 2, 2), 16)
        }

        return [pscustomobject]@{
            CanId = $canId
            Dlc   = $dlc
            Data  = $data
            Raw   = $trimmed
        }
    }
    catch {
        return $null
    }
}

function Send-SlcanCommand {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [string]$Command,
        [switch]$SuppressReply
    )

    Write-TraceLine "SLCAN -> $Command"

    if ($Run) {
        Wait-TxGap -GapMs 10
        $Serial.Write("$Command`r")
        $script:LastTxTickMs = [Environment]::TickCount64
        if ($SuppressReply) {
            return
        }

        Start-Sleep -Milliseconds 80

        if ($Serial.BytesToRead -gt 0) {
            $reply = $Serial.ReadExisting()
            if ($reply) {
                Write-TraceLine ("SLCAN <- " + (Format-Reply $reply))
            }
        }
    }
}

function Send-CanData {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [int]$CanId,
        [byte[]]$Data,
        [string]$Label,
        [switch]$SuppressReply
    )

    $slcan = New-SlcanFrame -CanId $CanId -Data $Data
    Write-TraceLine ("{0}: CAN ID 0x{1:X3}, DATA {2}" -f $Label, $CanId, (ConvertTo-PrettyHex $Data))
    Send-SlcanCommand -Serial $Serial -Command $slcan -SuppressReply:$SuppressReply
}

function Wait-CanReply {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [int]$CanId,
        [int]$ExpectedIndex,
        [int]$TimeoutMs = 80,
        [string]$Label = ""
    )

    if (-not $Run) {
        return $null
    }

    $buffer = ""
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    Write-TraceLine ("WAIT CAN reply: id=0x{0:X3}, index=0x{1:X4}, timeout={2} ms, label={3}" -f $CanId, $ExpectedIndex, $TimeoutMs, $Label)

    while ($sw.ElapsedMilliseconds -lt $TimeoutMs) {
        if ($Serial.BytesToRead -le 0) {
            Start-Sleep -Milliseconds 5
            continue
        }

        $buffer += $Serial.ReadExisting()
        foreach ($line in (Get-CompleteSerialLines ([ref]$buffer))) {
            $frame = Parse-SlcanLine -Line $line
            if (-not $frame) {
                Write-TraceLine ("RAW <- {0}" -f $line)
                continue
            }

            Write-TraceLine ("CAN <- 0x{0:X3}, DATA {1}" -f $frame.CanId, (ConvertTo-PrettyHex $frame.Data))
            if (($frame.CanId -ne $CanId) -or ($frame.Dlc -lt 6) -or ($frame.Data[0] -ne 0x42)) {
                continue
            }

            $index = ([int]$frame.Data[2] -shl 8) -bor [int]$frame.Data[1]
            if ($index -ne $ExpectedIndex) {
                continue
            }

            $wordValue = ([int]$frame.Data[5] -shl 8) -bor [int]$frame.Data[4]
            return [pscustomobject]@{
                Raw       = $frame.Raw
                CanId     = $frame.CanId
                Dlc       = $frame.Dlc
                Data      = $frame.Data
                Index     = $index
                WordValue = $wordValue
            }
        }
    }

    throw ("CAN reply timeout: id=0x{0:X3}, index=0x{1:X4}, timeout={2} ms, label={3}" -f $CanId, $ExpectedIndex, $TimeoutMs, $Label)
}

function Request-CanRead16 {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [int]$CanId,
        [int]$Register,
        [string]$Label,
        [int]$TimeoutMs = 80
    )

    $data = New-Read16Data -Register $Register
    $expectedIndex = 0x4000 + $Register
    Send-CanData -Serial $Serial -CanId $CanId -Data $data -Label $Label -SuppressReply
    return Wait-CanReply -Serial $Serial -CanId $CanId -ExpectedIndex $expectedIndex -TimeoutMs $TimeoutMs -Label $Label
}

function Read-SteeringPosition {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [int]$CanId
    )

    $highReply = Request-CanRead16 -Serial $Serial -CanId $CanId -Register 0x13 -Label "Read steering position high" -TimeoutMs 80
    $lowReply = Request-CanRead16 -Serial $Serial -CanId $CanId -Register 0x14 -Label "Read steering position low" -TimeoutMs 80

    if (($null -eq $highReply) -or ($null -eq $lowReply)) {
        return $null
    }

    $raw = ([int64]([uint32]$highReply.WordValue) -shl 16) -bor [uint32]$lowReply.WordValue
    if (($raw -band 0x80000000L) -ne 0) {
        $raw -= 0x100000000L
    }

    return [pscustomobject]@{
        PositionRaw = [int64]$raw
        HighWord    = $highReply.WordValue
        LowWord     = $lowReply.WordValue
    }
}

$clearStop = New-Write16Data -Register 0x30 -Value 0
$speedCmd = New-Write16Data -Register 0x31 -Value $SpeedRpm
$zeroSpeed = New-Write16Data -Register 0x31 -Value 0
$stopCmd = New-Write16Data -Register 0x30 -Value $StopMode
$speedLoopModeCmd = New-Write16Data -Register 0x6B -Value 0
$controlModeCmd = New-Write16Data -Register 0x6C -Value $ControlMode

Write-TraceLine "MSSC steering motor CAN control"
Write-TraceLine ("Port={0}, bitrate=500kbps, node={1}, run={2}, readPosition={3}" -f $Port, $NodeId, [bool]$Run, [bool]$ReadPosition)
Write-TraceLine ("LoopMode=0(speed), ControlMode={0}, SpeedRpm={1}" -f $ControlMode, $SpeedRpm)

if (-not $Run) {
    Write-TraceLine "Dry run only. Add -Run to actually transmit frames."
}

$serial = $null
try {
    if ($Run) {
        $serial = [System.IO.Ports.SerialPort]::new($Port, 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
        $serial.ReadTimeout = 250
        $serial.WriteTimeout = 250
        $serial.NewLine = "`r"
        $serial.Open()
    }

    Send-SlcanCommand -Serial $serial -Command "C"
    Send-SlcanCommand -Serial $serial -Command "S6"
    Send-SlcanCommand -Serial $serial -Command "O"

    if ($ReadPosition) {
        try {
            $initialPosition = Read-SteeringPosition -Serial $serial -CanId $NodeId
            if ($initialPosition) {
                Write-TraceLine ("Initial steering position raw={0}, high=0x{1:X4}, low=0x{2:X4}" -f $initialPosition.PositionRaw, $initialPosition.HighWord, $initialPosition.LowWord)
            }
        } catch {
            Write-TraceLine $_.Exception.Message "WARN"
        }
    }

    if ($StopOnly) {
        Send-CanData -Serial $serial -CanId $NodeId -Data $speedLoopModeCmd -Label "Set speed loop mode"
        Send-CanData -Serial $serial -CanId $NodeId -Data $controlModeCmd -Label ("Set control mode {0}" -f $ControlMode)
        Send-CanData -Serial $serial -CanId $NodeId -Data $zeroSpeed -Label "Steering motor 0 RPM"
        Send-CanData -Serial $serial -CanId $NodeId -Data $stopCmd -Label ("Steering stop mode {0}" -f $StopMode)
    }
    else {
        Send-CanData -Serial $serial -CanId $NodeId -Data $speedLoopModeCmd -Label "Set speed loop mode"
        Send-CanData -Serial $serial -CanId $NodeId -Data $controlModeCmd -Label ("Set control mode {0}" -f $ControlMode)
        Send-CanData -Serial $serial -CanId $NodeId -Data $clearStop -Label "Clear stop state"
        Send-CanData -Serial $serial -CanId $NodeId -Data $speedCmd -Label ("Steering motor {0} RPM" -f $SpeedRpm)
        Write-TraceLine ("Hold for {0} second(s)..." -f $DurationSeconds)

        if ($Run) {
            Start-Sleep -Milliseconds ([int]($DurationSeconds * 1000))
        }

        Send-CanData -Serial $serial -CanId $NodeId -Data $zeroSpeed -Label "Steering motor 0 RPM"
        Send-CanData -Serial $serial -CanId $NodeId -Data $stopCmd -Label ("Steering stop mode {0}" -f $StopMode)
    }

    if ($ReadPosition) {
        try {
            $finalPosition = Read-SteeringPosition -Serial $serial -CanId $NodeId
            if ($finalPosition) {
                Write-TraceLine ("Final steering position raw={0}, high=0x{1:X4}, low=0x{2:X4}" -f $finalPosition.PositionRaw, $finalPosition.HighWord, $finalPosition.LowWord)
            }
        } catch {
            Write-TraceLine $_.Exception.Message "WARN"
        }
    }
}
finally {
    if ($serial -and $serial.IsOpen) {
        Send-SlcanCommand -Serial $serial -Command "C"
        $serial.Close()
    }
}
