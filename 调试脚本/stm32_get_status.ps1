# STM32 USB CDC one-shot diagnostic script.
# Sends get_status / get_calibration / query_linear_steering and prints
# whatever the firmware replies. Used to find out why the steering motor
# only "hums" but does not turn.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\stm32_get_status.ps1 -ListPorts
#   powershell -ExecutionPolicy Bypass -File .\stm32_get_status.ps1 -Port COMx
#   powershell -ExecutionPolicy Bypass -File .\stm32_get_status.ps1 -Port COMx -Continuous
#   powershell -ExecutionPolicy Bypass -File .\stm32_get_status.ps1 -Port COMx -DtrEnable -RtsEnable

param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [switch]$ListPorts,
    [switch]$Continuous,
    [switch]$DtrEnable,
    [switch]$RtsEnable
)

$ErrorActionPreference = "Stop"

function Write-TraceLine {
    param(
        [string]$Message,
        [string]$Level = "INFO"
    )
    $ts = Get-Date -Format "HH:mm:ss.fff"
    Write-Host ("[{0}] [{1}] {2}" -f $ts, $Level, $Message)
}

function Show-AvailablePorts {
    Write-Host ""
    Write-Host "Available COM ports (look for STMicroelectronics / Virtual COM):" -ForegroundColor Cyan
    Get-CimInstance -ClassName Win32_PnPEntity -Filter "ConfigManagerErrorCode = 0" |
        Where-Object { $_.Name -match "\(COM\d+\)" } |
        Select-Object Name, DeviceID |
        Format-Table -AutoSize

    Write-Host "Tip: unplug the STM32 USB and run -ListPorts again to see which COM disappears." -ForegroundColor Yellow
}

function Invoke-DiagnosticRound {
    param(
        [string]$Port,
        [int]$Baud,
        [bool]$DtrEnabled,
        [bool]$RtsEnabled
    )

    $job = Start-Job -ArgumentList $Port, $Baud, $DtrEnabled, $RtsEnabled -ScriptBlock {
        param(
            [string]$WorkerPort,
            [int]$WorkerBaud,
            [bool]$WorkerDtrEnabled,
            [bool]$WorkerRtsEnabled
        )

        $records = New-Object System.Collections.Generic.List[string]

        function Send-WorkerCommand {
            param(
                [System.IO.Ports.SerialPort]$Serial,
                [string]$Command,
                [int]$ListenMs = 700
            )

            $records.Add("INFO`tTX: $Command") | Out-Null
            $bytes = [System.Text.Encoding]::ASCII.GetBytes($Command + "`n")
            $Serial.BaseStream.Write($bytes, 0, $bytes.Length)
            $Serial.BaseStream.Flush()

            $deadline = [Environment]::TickCount64 + $ListenMs
            $buffer = ""
            while ([Environment]::TickCount64 -lt $deadline) {
                if ($Serial.BytesToRead -gt 0) {
                    $buffer += $Serial.ReadExisting()
                } else {
                    Start-Sleep -Milliseconds 20
                }
            }

            if ([string]::IsNullOrWhiteSpace($buffer)) {
                $records.Add("WARN`t(no reply)") | Out-Null
                return
            }

            foreach ($line in $buffer -split "`r?`n") {
                $trim = $line.Trim()
                if ($trim) {
                    $records.Add("INFO`tRX: $trim") | Out-Null
                }
            }
        }

        $serial = $null
        try {
            $records.Add(("INFO`tOpen {0} @ {1} 8N1" -f $WorkerPort, $WorkerBaud)) | Out-Null
            $serial = [System.IO.Ports.SerialPort]::new($WorkerPort, $WorkerBaud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
            $serial.ReadTimeout = 250
            $serial.WriteTimeout = 250
            $serial.NewLine = "`n"
            $serial.DtrEnable = $WorkerDtrEnabled
            $serial.RtsEnable = $WorkerRtsEnabled
            $serial.Open()

            Start-Sleep -Milliseconds 200
            if ($serial.BytesToRead -gt 0) {
                $junk = $serial.ReadExisting()
                if ($junk) {
                    $records.Add(("INFO`tflushed " + $junk.Length + " bytes of warm-up data")) | Out-Null
                }
            }

            Send-WorkerCommand -Serial $serial -Command '{"cmd":"get_status"}'
            Send-WorkerCommand -Serial $serial -Command '{"cmd":"get_calibration"}'
            Send-WorkerCommand -Serial $serial -Command '{"cmd":"query_linear_steering"}'
        }
        catch {
            $records.Add(("ERROR`tserial diagnostic failed: " + $_.Exception.Message)) | Out-Null
        }
        finally {
            if ($serial -and $serial.IsOpen) {
                $serial.Close()
            }
        }

        $records
    }

    if (Wait-Job $job -Timeout 10) {
        Receive-Job $job | ForEach-Object {
            $parts = $_ -split "`t", 2
            if ($parts.Count -eq 2) {
                Write-TraceLine $parts[1] $parts[0]
            } else {
                Write-TraceLine $_
            }
        }
    } else {
        Stop-Job $job
        Write-TraceLine "serial diagnostic timed out; check whether another tool is holding the COM port" "ERROR"
    }

    Remove-Job $job -Force
}

if ($ListPorts) {
    Show-AvailablePorts
    return
}

if (-not $Port) {
    Write-TraceLine "No -Port given, listing all available ports below." "WARN"
    Show-AvailablePorts
    return
}

do {
    Write-Host ""
    Write-Host "========== diagnostic round ==========" -ForegroundColor Cyan
    Invoke-DiagnosticRound -Port $Port -Baud $Baud -DtrEnabled ([bool]$DtrEnable) -RtsEnabled ([bool]$RtsEnable)

    if ($Continuous) {
        Start-Sleep -Seconds 2
    }
} while ($Continuous)
