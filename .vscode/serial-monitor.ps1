param(
    [string]$Port = "",
    [int]$BaudRate = 115200,
    [int]$DurationSec = 0,
    [string]$OutputPath = "",
    [switch]$Append
)

$ErrorActionPreference = "Stop"

function Show-SerialPorts {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object

    if ($ports.Count -eq 0) {
        Write-Host "No serial ports found."
        return
    }

    Write-Host "Available serial ports:"
    foreach ($name in $ports) {
        $desc = ""
        try {
            $info = Get-CimInstance Win32_SerialPort |
                Where-Object { $_.DeviceID -eq $name } |
                Select-Object -First 1
            if ($null -ne $info) {
                $desc = "  $($info.Name)"
            }
        } catch {
            $desc = ""
        }

        Write-Host "  $name$desc"
    }
}

if ([string]::IsNullOrWhiteSpace($Port)) {
    Show-SerialPorts
    Write-Host ""
    Write-Host "Run again with a port, for example:"
    Write-Host "  powershell -NoProfile -ExecutionPolicy Bypass -File .vscode/serial-monitor.ps1 -Port COM3"
    exit 0
}

$serial = [System.IO.Ports.SerialPort]::new(
    $Port,
    $BaudRate,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)

$serial.Encoding = [System.Text.Encoding]::ASCII
$serial.NewLine = "`n"
$serial.ReadTimeout = 200
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$writer = $null
$paused = $false

try {
    if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
        $fullOutputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
        $outputDir = Split-Path -Parent $fullOutputPath
        if (-not [string]::IsNullOrWhiteSpace($outputDir)) {
            New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
        }
        $writer = [System.IO.StreamWriter]::new($fullOutputPath, [bool]$Append, [System.Text.Encoding]::ASCII)
    }

    $serial.Open()
    Write-Host "Serial monitor opened: $Port @ $BaudRate 8N1"
    if ($DurationSec -gt 0) {
        Write-Host "Capture duration: $DurationSec seconds"
    }
    if ($null -ne $writer) {
        Write-Host "Saving raw CSV to: $fullOutputPath"
    }
    Write-Host "Keys: P=pause/resume, Q=quit, Ctrl+C=stop."
    Write-Host ""

    $endTime = $null
    if ($DurationSec -gt 0) {
        $endTime = (Get-Date).AddSeconds($DurationSec)
    }

    while ($true) {
        if (($null -ne $endTime) -and ((Get-Date) -ge $endTime)) {
            break
        }

        try {
            if ([Console]::KeyAvailable) {
                $key = [Console]::ReadKey($true)
                if ($key.Key -eq [ConsoleKey]::P) {
                    $paused = -not $paused
                    if ($paused) {
                        Write-Host "[paused]"
                    } else {
                        Write-Host "[resumed]"
                    }
                } elseif ($key.Key -eq [ConsoleKey]::Q) {
                    break
                }
            }
        } catch {
        }

        if ($paused) {
            Start-Sleep -Milliseconds 100
            continue
        }

        try {
            $line = $serial.ReadLine()
            $line = $line.TrimEnd("`r", "`n")
            Write-Host $line
            if ($null -ne $writer) {
                $writer.WriteLine($line)
            }
        } catch [System.TimeoutException] {
        }
    }
} finally {
    if ($null -ne $writer) {
        $writer.Flush()
        $writer.Close()
    }
    if ($serial.IsOpen) {
        $serial.Close()
    }
}
