param(
    [string]$Port = "COM5",
    [ValidateRange(1, 127)]
    [int]$NodeId = 1,
    [int]$RightRpm = 100,
    [int]$LeftRpm = -100,
    [ValidateRange(0.1, 60)]
    [double]$DurationSeconds = 3,
    [switch]$Run,
    [switch]$StopOnly
)

$ErrorActionPreference = "Stop"

function ConvertTo-Unsigned32 {
    param([int]$Value)

    if ($Value -lt 0) {
        return [uint32]([int64]$Value + 0x100000000)
    }

    return [uint32]$Value
}

function New-Write32SpeedData {
    param(
        [ValidateSet(0x39, 0x3D)]
        [int]$Register,
        [int]$Rpm
    )

    $index = 0x4000 + $Register
    $raw = ConvertTo-Unsigned32 $Rpm
    $highWord = ($raw -shr 16) -band 0xFFFF
    $lowWord = $raw -band 0xFFFF

    return [byte[]]@(
        0x24,
        ($index -band 0xFF),
        (($index -shr 8) -band 0xFF),
        0x00,
        ($highWord -band 0xFF),
        (($highWord -shr 8) -band 0xFF),
        ($lowWord -band 0xFF),
        (($lowWord -shr 8) -band 0xFF)
    )
}

function New-Write16Data {
    param(
        [int]$Register,
        [int]$Value
    )

    $index = 0x4000 + $Register
    $raw = $Value -band 0xFFFF

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
                Write-Host ("SLCAN <- " + ($reply -replace "`r", "\r" -replace "`n", "\n"))
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

$rightSpeed = New-Write32SpeedData -Register 0x39 -Rpm $RightRpm
$leftSpeed = New-Write32SpeedData -Register 0x3D -Rpm $LeftRpm
$rightZero = New-Write32SpeedData -Register 0x39 -Rpm 0
$leftZero = New-Write32SpeedData -Register 0x3D -Rpm 0

# Register 0x38 is documented as right emergency stop. 0x3C follows the same
# dual-motor register layout for the left motor, while zero-speed remains the
# primary normal stop command.
$rightEmergencyStop = New-Write16Data -Register 0x38 -Value 1
$leftEmergencyStop = New-Write16Data -Register 0x3C -Value 1

Write-Host "MSSD-60EHB_2D dual-wheel low-speed CAN control"
Write-Host ("Port={0}, bitrate=500kbps, node={1}, run={2}" -f $Port, $NodeId, [bool]$Run)

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
        Send-CanData -Serial $serial -CanId $NodeId -Data $rightZero -Label "Right motor 0 RPM"
        Send-CanData -Serial $serial -CanId $NodeId -Data $leftZero -Label "Left motor 0 RPM"
        Send-CanData -Serial $serial -CanId $NodeId -Data $rightEmergencyStop -Label "Right motor emergency stop"
        Send-CanData -Serial $serial -CanId $NodeId -Data $leftEmergencyStop -Label "Left motor emergency stop"
    }
    else {
        Send-CanData -Serial $serial -CanId $NodeId -Data $rightSpeed -Label ("Right motor {0} RPM" -f $RightRpm)
        Send-CanData -Serial $serial -CanId $NodeId -Data $leftSpeed -Label ("Left motor {0} RPM" -f $LeftRpm)
        Write-Host ("Hold for {0} second(s)..." -f $DurationSeconds)
        if ($Run) {
            Start-Sleep -Milliseconds ([int]($DurationSeconds * 1000))
        }
        Send-CanData -Serial $serial -CanId $NodeId -Data $rightZero -Label "Right motor 0 RPM"
        Send-CanData -Serial $serial -CanId $NodeId -Data $leftZero -Label "Left motor 0 RPM"
    }
}
finally {
    if ($serial -and $serial.IsOpen) {
        Send-SlcanCommand -Serial $serial -Command "C"
        $serial.Close()
    }
}
