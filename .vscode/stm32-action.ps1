param(
    [Parameter(Mandatory = $true)]
    [string]$Action
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

$make = "D:/MAKE/make.exe"
$openocd = "D:/OPENOCD/xpack-openocd-0.12.0-7/bin/openocd.exe"
$elf = "build/spi_bmi088.elf"
$hex = "build/spi_bmi088.hex"
$bin = "build/spi_bmi088.bin"
$openocdArgs = @(
    "-f", "interface/stlink.cfg",
    "-f", "target/stm32f4x.cfg",
    "-c", "adapter speed 1000"
)

function Write-Section {
    param([string]$Message)
    Write-Host ""
    Write-Host "============================================================" -ForegroundColor DarkCyan
    Write-Host $Message -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor DarkCyan
}

function Write-Info {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Gray
}

function Write-Ok {
    param([string]$Message)
    Write-Host "[OK] $Message" -ForegroundColor Green
}

function Write-Fail {
    param([string]$Message)
    Write-Host "[FAIL] $Message" -ForegroundColor Red
}

function Write-Warn {
    param([string]$Message)
    Write-Host "[WARN] $Message" -ForegroundColor Yellow
}

function Write-Hint {
    param([string]$Message)
    Write-Host "  - $Message" -ForegroundColor Yellow
}

function Test-Tool {
    param(
        [string]$Path,
        [string]$Name
    )

    if (-not (Test-Path $Path)) {
        throw "$Name 工具不存在：$Path"
    }
}

function Get-FirstImportantError {
    param([string[]]$Lines)

    foreach ($line in $Lines) {
        if ($line -match "(?i)(\berror\b|\bfatal\b|\bfailed\b|cannot|unable|not found|No such file|undefined reference|找不到|失败|错误)") {
            return $line
        }
    }

    return $null
}

function Invoke-ExternalCommand {
    param(
        [string]$Stage,
        [string]$Program,
        [string[]]$ArgsList,
        [string[]]$FailureHints = @(),
        [string]$SuccessMarker = ""
    )

    Write-Section "开始：$Stage"
    Write-Info "命令：$Program $($ArgsList -join ' ')"
    $output = New-Object System.Collections.Generic.List[string]

    # 关键：OpenOCD 把 Info/Warn 全打到 stderr。用 $ErrorActionPreference=Stop 时,
    # 2>&1 管道里的 stderr 行会被包装成 ErrorRecord 并可能污染 $LASTEXITCODE。
    # 这里在合并流的范围内临时放开 Stop,保证只用真正的进程退出码判成败。
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $Program @ArgsList 2>&1 | ForEach-Object {
        $line = $_.ToString()
        $output.Add($line) | Out-Null
        Write-Host $line
    }
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $prevEap

    if ($null -eq $exitCode) {
        $exitCode = 0
    }

    # 成功标志兜底:某些 OpenOCD/管道组合下退出码不可靠,
    # 但输出里出现 Verified OK 等标志时,以标志为准判成功。
    if ($exitCode -ne 0 -and $SuccessMarker -ne "") {
        foreach ($line in $output.ToArray()) {
            if ($line -match [regex]::Escape($SuccessMarker)) {
                Write-Warn "$Stage 进程退出码为 $exitCode，但检测到成功标志「$SuccessMarker」，判定为成功。"
                return $output.ToArray()
            }
        }
    }

    if ($exitCode -ne 0) {
        Write-Fail "$Stage 失败，退出码：$exitCode"
        $firstError = Get-FirstImportantError -Lines $output.ToArray()
        if ($firstError) {
            Write-Fail "关键错误：$firstError"
        }
        if ($FailureHints.Count -gt 0) {
            Write-Host "可能原因 / 检查方向：" -ForegroundColor Yellow
            foreach ($hint in $FailureHints) {
                Write-Hint $hint
            }
        }
        throw "$Stage failed with exit code $exitCode"
    }

    return $output.ToArray()
}

function Write-BuildArtifacts {
    $artifacts = @($elf, $hex, $bin)

    foreach ($artifact in $artifacts) {
        if (Test-Path $artifact) {
            $item = Get-Item $artifact
            Write-Info ("输出文件：{0}  大小：{1} bytes" -f $artifact, $item.Length)
        } else {
            Write-Hint "未找到输出文件：$artifact"
        }
    }
}

function Invoke-Make {
    param([string[]]$ArgsList = @())
    Test-Tool $make "make"
    New-Item -ItemType Directory -Force -Path "build" | Out-Null
    $output = Invoke-ExternalCommand `
        -Stage "编译工程" `
        -Program $make `
        -ArgsList $ArgsList `
        -FailureHints @(
            "先看上面 gcc 输出的第一条 error，通常那一行就是根因。",
            "检查 include 路径、宏定义、函数声明、变量名是否拼错。",
            "如果提示找不到 arm-none-eabi-gcc，检查 GCC 是否加入 PATH。"
        )

    if ($output -match "Nothing to be done") {
        Write-Ok "编译检查通过：源码没有变化，无需重新编译。"
    } else {
        Write-Ok "编译成功：固件已生成或已更新。"
    }

    Write-BuildArtifacts

    if (-not (Test-Path $elf)) {
        Write-Fail "编译结束但没有找到烧录文件：$elf"
        throw "build output missing: $elf"
    }
}

function Remove-BuildDirectory {
    Write-Section "开始：清理 build 目录"
    Remove-Item -Recurse -Force "build" -ErrorAction SilentlyContinue
    Write-Ok "清理完成：build 目录已删除或原本不存在。"
}

function Invoke-OpenOCD {
    param([string[]]$ExtraArgs = @())
    Test-Tool $openocd "OpenOCD"
    $isFlashAction = (($ExtraArgs -join " ") -match "\bprogram\b")
    $successMarker = if ($isFlashAction) { "Verified OK" } else { "" }

    # ST-Link V2 在一次会话 shutdown 后 USB 端点释放有延迟,
    # 快速连续调用时会间歇性 "open failed / init failed"。这是瞬时竞争,
    # 不是接线或固件问题,自动等待重试即可消除。仅对烧录动作重试。
    $maxAttempts = if ($isFlashAction) { 3 } else { 1 }
    $attempt = 0
    $output = $null

    while ($true) {
        $attempt++
        try {
            $output = Invoke-ExternalCommand `
                -Stage "烧录 / 调试连接" `
                -Program $openocd `
                -ArgsList ($openocdArgs + $ExtraArgs) `
                -SuccessMarker $successMarker `
                -FailureHints @(
                    "检查 ST-Link 是否插好，目标板是否上电。",
                    "检查 SWDIO/SWCLK/GND/3V3 是否连接正确。",
                    "如果提示找不到芯片，降低 adapter speed 或按住 reset 再试。",
                    "如果提示 flash/verify 失败，确认当前 elf/hex 是最新编译生成的。"
                )
            break
        } catch {
            $transient = ($_.Exception.Message -match "(?i)open failed|init failed") -or $false
            # 也检查已捕获输出里的瞬时错误特征(异常消息只带退出码时)。
            if (-not $transient -and $null -ne $output) {
                foreach ($line in $output) {
                    if ($line -match "(?i)open failed|init failed") { $transient = $true; break }
                }
            }
            if ($isFlashAction -and $attempt -lt $maxAttempts) {
                Write-Warn "第 $attempt 次烧录连接失败(疑似 ST-Link USB 瞬时占用)，等待后自动重试…"
                Start-Sleep -Seconds 2
                continue
            }
            throw
        }
    }

    if ($isFlashAction) {
        if ($output -match "Verified OK") {
            Write-Ok "烧录成功：写入完成，校验通过，目标板已复位。"
        } else {
            Write-Warn "OpenOCD 退出码为 0，但没有检测到 Verified OK；请确认上方是否完成 verify。"
        }
    } else {
        Write-Ok "OpenOCD 已启动：调试连接建立成功。"
    }
}

function Assert-FirmwareExists {
    if (-not (Test-Path $elf)) {
        Write-Hint "未找到 $elf，将先执行编译。"
        Invoke-Make
    }
}

try {
    Write-Section "STM32 动作：$Action"

    switch ($Action) {
        "STM32: build" {
            Invoke-Make
        }
        "STM32: clean" {
            Remove-BuildDirectory
        }
        "STM32: rebuild" {
            Remove-BuildDirectory
            Invoke-Make
        }
        "STM32: flash" {
            Assert-FirmwareExists
            Invoke-OpenOCD @("-c", "program $elf verify reset exit")
        }
        "STM32: build and flash" {
            Invoke-Make
            Invoke-OpenOCD @("-c", "program $elf verify reset exit")
        }
        "STM32: openocd server" {
            Invoke-OpenOCD
        }
        default {
            throw "未知 STM32 动作：$Action"
        }
    }

    Write-Section "完成：$Action"
    Write-Ok "任务执行成功。"
    exit 0
} catch {
    Write-Section "失败：$Action"
    Write-Fail $_.Exception.Message
    Write-Host "请优先查看上方第一处 [FAIL] 或 gcc/openocd 的第一条 error。" -ForegroundColor Yellow
    Write-Host "如果你把终端输出发给我，我会按阶段帮你定位：编译、连接、烧录、校验还是复位。" -ForegroundColor Yellow
    exit 1
}
