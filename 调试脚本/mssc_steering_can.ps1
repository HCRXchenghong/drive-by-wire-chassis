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
    [switch]$StopOnly
)

$ErrorActionPreference = "Stop"

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

function Send-SlcanCommand {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [string]$Command
    )

    Write-Host "SLCAN -> $Command"

    if ($Run) {
        $Serial.Write("$Command`r")
        Start-Sleep -Milliseconds 80

        if ($Serial.BytesToRead -gt 0) {
            $reply = $Serial.ReadExisting()
            if ($reply) {
                Write-Host ("SLCAN <- " + (Format-Reply $reply))
            }
        }
    }
}

function Send-CanData {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [int]$CanId,
        [byte[]]$Data,
        [string]$Label
    )

    $slcan = New-SlcanFrame -CanId $CanId -Data $Data
    Write-Host ("{0}: CAN ID 0x{1:X3}, DATA {2}" -f $Label, $CanId, (ConvertTo-PrettyHex $Data))
    Send-SlcanCommand -Serial $Serial -Command $slcan
}

$clearStop = New-Write16Data -Register 0x30 -Value 0
$speedCmd = New-Write16Data -Register 0x31 -Value $SpeedRpm
$zeroSpeed = New-Write16Data -Register 0x31 -Value 0
$stopCmd = New-Write16Data -Register 0x30 -Value $StopMode
$speedLoopModeCmd = New-Write16Data -Register 0x6B -Value 0
$controlModeCmd = New-Write16Data -Register 0x6C -Value $ControlMode

Write-Host "MSSC steering motor CAN control"
Write-Host ("Port={0}, bitrate=500kbps, node={1}, run={2}" -f $Port, $NodeId, [bool]$Run)
Write-Host ("LoopMode=0(speed), ControlMode={0}, SpeedRpm={1}" -f $ControlMode, $SpeedRpm)

if (-not $Run) {
    Write-Host "Dry run only. Add -Run to actually transmit frames."
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
        Write-Host ("Hold for {0} second(s)..." -f $DurationSeconds)

        if ($Run) {
            Start-Sleep -Milliseconds ([int]($DurationSeconds * 1000))
        }

        Send-CanData -Serial $serial -CanId $NodeId -Data $zeroSpeed -Label "Steering motor 0 RPM"
        Send-CanData -Serial $serial -CanId $NodeId -Data $stopCmd -Label ("Steering stop mode {0}" -f $StopMode)
    }
}
finally {
    if ($serial -and $serial.IsOpen) {
        Send-SlcanCommand -Serial $serial -Command "C"
        $serial.Close()
    }
}
