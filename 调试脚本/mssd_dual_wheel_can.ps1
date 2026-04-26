param(
    [string]$Port = "COM9",
    [ValidateRange(1, 127)]
    [int]$NodeId = 1,
    [int]$RightRpm = 100,
    [int]$LeftRpm = -100,
    [ValidateRange(0.1, 60)]
    [double]$DurationSeconds = 3,
    [ValidateRange(20, 2000)]
    [int]$FeedbackPollMs = 120,
    [ValidateRange(1, 200)]
    [int]$FeedbackSamples = 8,
    [switch]$Run,
    [switch]$StopOnly,
    [switch]$ReadFeedback,
    [switch]$SequenceTest,
    [switch]$DirectionCheck,
    [switch]$EmergencyStopOnStop
)

$ErrorActionPreference = "Stop"

function ConvertTo-Unsigned32 {
    param([int]$Value)

    if ($Value -lt 0) {
        return [uint32]([int64]$Value + 0x100000000)
    }

    return [uint32]$Value
}

function ConvertTo-Signed32 {
    param([uint32]$Value)

    if (($Value -band 0x80000000) -ne 0) {
        return [int]([int64]$Value - 0x100000000)
    }

    return [int]$Value
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

function Read-SerialQuiet {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [int]$QuietMs = 120,
        [int]$MaxMs = 1000
    )

    if (-not $Serial) {
        return ""
    }

    $buffer = ""
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $lastRxMs = -1

    do {
        Start-Sleep -Milliseconds 25
        if ($Serial.BytesToRead -gt 0) {
            $buffer += $Serial.ReadExisting()
            $lastRxMs = $sw.ElapsedMilliseconds
        }

        if (($lastRxMs -ge 0) -and (($sw.ElapsedMilliseconds - $lastRxMs) -ge $QuietMs)) {
            break
        }
    }
    while ($sw.ElapsedMilliseconds -lt $MaxMs)

    return $buffer
}

function Send-SlcanCommand {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [string]$Command,
        [switch]$SuppressReply
    )

    Write-Host "SLCAN -> $Command"
    if (-not $Run) {
        return ""
    }

    $Serial.Write("$Command`r")
    if ($SuppressReply) {
        return ""
    }

    $reply = Read-SerialQuiet -Serial $Serial -QuietMs 80 -MaxMs 400
    if ((-not $SuppressReply) -and $reply) {
        Write-Host ("SLCAN <- " + ($reply -replace "`r", "\r" -replace "`n", "\n"))
    }

    return $reply
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
    Write-Host ("{0}: CAN ID 0x{1:X3}, DATA {2}" -f $Label, $CanId, (ConvertTo-PrettyHex $Data))
    return Send-SlcanCommand -Serial $Serial -Command $slcan -SuppressReply:$SuppressReply
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

function ConvertTo-FeedbackSnapshot {
    param(
        [hashtable]$Words
    )

    $rightReady = $Words.ContainsKey("RightHigh") -and $Words.ContainsKey("RightLow")
    $leftReady = $Words.ContainsKey("LeftHigh") -and $Words.ContainsKey("LeftLow")

    $rightRpm = $null
    $leftRpm = $null

    if ($rightReady) {
        $rightRaw = ([uint32]$Words.RightHigh -shl 16) -bor [uint32]$Words.RightLow
        $rightRpm = ConvertTo-Signed32 -Value $rightRaw
    }

    if ($leftReady) {
        $leftRaw = ([uint32]$Words.LeftHigh -shl 16) -bor [uint32]$Words.LeftLow
        $leftRpm = ConvertTo-Signed32 -Value $leftRaw
    }

    return [pscustomobject]@{
        RightReady = $rightReady
        LeftReady  = $leftReady
        RightRpm   = $rightRpm
        LeftRpm    = $leftRpm
    }
}

function Get-FeedbackSnapshot {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [int]$CanId,
        [int]$PollMs,
        [int]$Samples
    )

    $words = @{}
    $rxLines = New-Object System.Collections.Generic.List[string]

    for ($sample = 1; $sample -le $Samples; $sample++) {
        foreach ($reg in 0x09, 0x0A, 0x14, 0x15) {
            $label = "Feedback read 0x{0:X2} (sample {1}/{2})" -f $reg, $sample, $Samples
            Send-CanData -Serial $Serial -CanId $CanId -Data (New-Read16Data -Register $reg) -Label $label -SuppressReply | Out-Null
        }

        if ($Run) {
            Start-Sleep -Milliseconds $PollMs
            $reply = Read-SerialQuiet -Serial $Serial -QuietMs 80 -MaxMs ([Math]::Max(200, $PollMs + 200))
            if ($reply) {
                $lines = $reply -split "(\r\n|\n|\r)"
                foreach ($line in $lines) {
                    $frame = Parse-SlcanLine -Line $line
                    if (-not $frame) {
                        continue
                    }

                    $rxLines.Add($frame.Raw)
                    if (($frame.CanId -ne $CanId) -or ($frame.Dlc -lt 6)) {
                        continue
                    }

                    if (($frame.Data[0] -ne 0x42) -or ($frame.Data[3] -ne 0x00)) {
                        continue
                    }

                    $index = ([int]$frame.Data[2] -shl 8) -bor [int]$frame.Data[1]
                    $wordValue = ([int]$frame.Data[5] -shl 8) -bor [int]$frame.Data[4]

                    switch ($index) {
                        0x4009 { $words.RightHigh = $wordValue; break }
                        0x400A { $words.RightLow = $wordValue; break }
                        0x4014 { $words.LeftHigh = $wordValue; break }
                        0x4015 { $words.LeftLow = $wordValue; break }
                    }
                }
            }
        }
    }

    $snapshot = ConvertTo-FeedbackSnapshot -Words $words
    $snapshot | Add-Member -NotePropertyName RxFrameCount -NotePropertyValue $rxLines.Count
    $snapshot | Add-Member -NotePropertyName RawFrames -NotePropertyValue @($rxLines.ToArray())
    return $snapshot
}

function Show-FeedbackSnapshot {
    param(
        [string]$Title,
        $Snapshot
    )

    Write-Host ""
    Write-Host ("==== {0} ====" -f $Title)
    Write-Host ("Feedback frames received: {0}" -f $Snapshot.RxFrameCount)
    if ($Snapshot.RightReady) {
        Write-Host ("Right actual RPM : {0}" -f $Snapshot.RightRpm)
    }
    else {
        Write-Host "Right actual RPM : <incomplete>"
    }

    if ($Snapshot.LeftReady) {
        Write-Host ("Left actual RPM  : {0}" -f $Snapshot.LeftRpm)
    }
    else {
        Write-Host "Left actual RPM  : <incomplete>"
    }

    if ($Snapshot.RawFrames.Count -gt 0) {
        Write-Host "Raw reply frames:"
        foreach ($raw in $Snapshot.RawFrames) {
            Write-Host ("  " + $raw)
        }
    }
    else {
        Write-Host "Raw reply frames: <none>"
        Write-Host "Note: no CAN reply frames were captured on the current USB-CAN path. This run only confirms command transmission."
    }
    Write-Host ""
}

function Send-NormalStop {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [int]$CanId
    )

    Send-CanData -Serial $Serial -CanId $CanId -Data (New-Write32SpeedData -Register 0x39 -Rpm 0) -Label "Right motor 0 RPM" | Out-Null
    Send-CanData -Serial $Serial -CanId $CanId -Data (New-Write32SpeedData -Register 0x3D -Rpm 0) -Label "Left motor 0 RPM" | Out-Null
}

function Send-EmergencyStop {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [int]$CanId
    )

    Send-CanData -Serial $Serial -CanId $CanId -Data (New-Write16Data -Register 0x38 -Value 1) -Label "Right motor emergency stop" | Out-Null
    Send-CanData -Serial $Serial -CanId $CanId -Data (New-Write16Data -Register 0x3C -Value 1) -Label "Left motor emergency stop" | Out-Null
}

function Stop-Drive {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [int]$CanId,
        [bool]$UseEmergencyStop
    )

    Send-NormalStop -Serial $Serial -CanId $CanId
    if ($UseEmergencyStop) {
        Send-EmergencyStop -Serial $Serial -CanId $CanId
    }
}

function Invoke-Step {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [int]$CanId,
        [string]$Name,
        [int]$StepRightRpm,
        [int]$StepLeftRpm
    )

    Write-Host ""
    Write-Host ("==== {0} ====" -f $Name)
    Send-CanData -Serial $Serial -CanId $CanId -Data (New-Write32SpeedData -Register 0x39 -Rpm $StepRightRpm) -Label ("Right motor {0} RPM" -f $StepRightRpm) | Out-Null
    Send-CanData -Serial $Serial -CanId $CanId -Data (New-Write32SpeedData -Register 0x3D -Rpm $StepLeftRpm) -Label ("Left motor {0} RPM" -f $StepLeftRpm) | Out-Null
    Write-Host ("Hold for {0} second(s)..." -f $DurationSeconds)
    if ($Run) {
        Start-Sleep -Milliseconds ([int]($DurationSeconds * 1000))
    }

    if ($ReadFeedback) {
        $runningSnapshot = Get-FeedbackSnapshot -Serial $Serial -CanId $CanId -PollMs $FeedbackPollMs -Samples $FeedbackSamples
        Show-FeedbackSnapshot -Title ("Feedback while running - {0}" -f $Name) -Snapshot $runningSnapshot
    }

    Stop-Drive -Serial $Serial -CanId $CanId -UseEmergencyStop:$EmergencyStopOnStop

    if ($ReadFeedback) {
        if ($Run) {
            Start-Sleep -Milliseconds $FeedbackPollMs
        }
        $stopSnapshot = Get-FeedbackSnapshot -Serial $Serial -CanId $CanId -PollMs $FeedbackPollMs -Samples ([Math]::Max(3, [Math]::Min($FeedbackSamples, 8)))
        Show-FeedbackSnapshot -Title ("Feedback after stop - {0}" -f $Name) -Snapshot $stopSnapshot
    }
}

Write-Host "MSSD-60EHB_2D dual-wheel low-speed CAN control"
Write-Host ("Port={0}, bitrate=500kbps, node={1}, run={2}, readFeedback={3}, sequenceTest={4}, directionCheck={5}" -f $Port, $NodeId, [bool]$Run, [bool]$ReadFeedback, [bool]$SequenceTest, [bool]$DirectionCheck)

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

    Send-SlcanCommand -Serial $serial -Command "C" | Out-Null
    Send-SlcanCommand -Serial $serial -Command "S6" | Out-Null
    Send-SlcanCommand -Serial $serial -Command "O" | Out-Null

    if ($StopOnly) {
        Stop-Drive -Serial $serial -CanId $NodeId -UseEmergencyStop:$EmergencyStopOnStop
        if ($ReadFeedback) {
            $stopOnlySnapshot = Get-FeedbackSnapshot -Serial $serial -CanId $NodeId -PollMs $FeedbackPollMs -Samples $FeedbackSamples
            Show-FeedbackSnapshot -Title "Feedback after stop-only sequence" -Snapshot $stopOnlySnapshot
        }
    }
    elseif ($DirectionCheck) {
        $rightMagnitude = [Math]::Abs($RightRpm)
        $leftMagnitude = [Math]::Abs($LeftRpm)

        if ($rightMagnitude -le 0) {
            $rightMagnitude = 200
        }

        if ($leftMagnitude -le 0) {
            $leftMagnitude = 200
        }

        $steps = @(
            @{ Name = "Right wheel +RPM (observe physical direction)"; Right = $rightMagnitude; Left = 0 },
            @{ Name = "Right wheel -RPM (observe physical direction)"; Right = -$rightMagnitude; Left = 0 },
            @{ Name = "Left wheel +RPM (observe physical direction)"; Right = 0; Left = $leftMagnitude },
            @{ Name = "Left wheel -RPM (observe physical direction)"; Right = 0; Left = -$leftMagnitude }
        )

        foreach ($step in $steps) {
            Invoke-Step -Serial $serial -CanId $NodeId -Name $step.Name -StepRightRpm $step.Right -StepLeftRpm $step.Left
        }
    }
    elseif ($SequenceTest) {
        $steps = @(
            @{ Name = "Right wheel only"; Right = $RightRpm; Left = 0 },
            @{ Name = "Left wheel only"; Right = 0; Left = $LeftRpm },
            @{ Name = "Split dual-wheel"; Right = $RightRpm; Left = $LeftRpm }
        )

        foreach ($step in $steps) {
            Invoke-Step -Serial $serial -CanId $NodeId -Name $step.Name -StepRightRpm $step.Right -StepLeftRpm $step.Left
        }
    }
    else {
        Invoke-Step -Serial $serial -CanId $NodeId -Name "Manual dual-wheel run" -StepRightRpm $RightRpm -StepLeftRpm $LeftRpm
    }
}
finally {
    if ($serial -and $serial.IsOpen) {
        Send-SlcanCommand -Serial $serial -Command "C" -SuppressReply | Out-Null
        $serial.Close()
    }
}
