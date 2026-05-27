# ==============================================================================
#  setup.ps1 - Download and extract all project dependencies
#  Run ONCE before opening the Visual Studio solution
# ==============================================================================
$ErrorActionPreference = "Stop"
$ProgressPreference    = "SilentlyContinue"

$ProjectRoot  = $PSScriptRoot
$DepsDir      = Join-Path $ProjectRoot "deps"
$DownloadDir  = Join-Path $DepsDir     "_downloads"

foreach ($d in @($DepsDir, $DownloadDir)) {
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}

Write-Host ""
Write-Host "======================================================" -ForegroundColor Cyan
Write-Host "   YOLOv8 CS2 Overlay C++ -- Dependency Setup"         -ForegroundColor Cyan
Write-Host "======================================================" -ForegroundColor Cyan
Write-Host ""

# -- Helper: download with basic error handling --------------------------------
function Get-Dep {
    param(
        [string]$Url,
        [string]$Out,
        [string]$Label
    )
    if (Test-Path $Out) {
        $sz = [math]::Round((Get-Item $Out).Length / 1MB, 1)
        Write-Host ("  [SKIP] " + $Label + " (" + $sz + " MB already present)") -ForegroundColor Yellow
        return
    }
    Write-Host ("  [DL  ] " + $Label + " ...") -ForegroundColor White
    try {
        Invoke-WebRequest -Uri $Url -OutFile $Out -UseBasicParsing
        $sz = [math]::Round((Get-Item $Out).Length / 1MB, 1)
        Write-Host ("  [OK  ] " + $Label + " (" + $sz + " MB)") -ForegroundColor Green
    } catch {
        Write-Host ("  [FAIL] " + $Label + " : " + $_) -ForegroundColor Red
        Write-Host ""
        Write-Host "  Download manually from:" -ForegroundColor Yellow
        Write-Host ("  " + $Url) -ForegroundColor Yellow
        Write-Host ("  and save to: " + $Out) -ForegroundColor Yellow
        throw
    }
}

# ==============================================================================
#  1.  OpenCV 4.9.0  (official 7-zip SFX archive, ~260 MB)
# ==============================================================================
Write-Host "[1/2]  OpenCV 4.9.0" -ForegroundColor Cyan

$OCV_VER  = "4.9.0"
$OCV_EXE  = Join-Path $DownloadDir "opencv-$OCV_VER-windows.exe"
$OCV_ROOT = Join-Path $DepsDir     "opencv"
$OCV_URL  = "https://github.com/opencv/opencv/releases/download/$OCV_VER/opencv-$OCV_VER-windows.exe"

Get-Dep -Url $OCV_URL -Out $OCV_EXE -Label "OpenCV $OCV_VER"

if (-not (Test-Path (Join-Path $OCV_ROOT "build\include\opencv2"))) {
    Write-Host "  [EXTR] Extracting OpenCV (may take 1-2 min)..." -ForegroundColor White
    # OpenCV .exe is a 7-zip SFX archive: -o sets output dir, -y answers yes to all
    $p = Start-Process -Wait -PassThru -FilePath $OCV_EXE `
             -ArgumentList @(("-o" + $DepsDir), "-y")
    if ($p.ExitCode -ne 0) {
        throw ("OpenCV extraction failed (exit code " + $p.ExitCode + ")")
    }
    Write-Host "  [OK  ] OpenCV extracted." -ForegroundColor Green
} else {
    Write-Host "  [SKIP] OpenCV already extracted." -ForegroundColor Yellow
}

# Detect which vc folder was compiled (vc16=VS2019 or vc17=VS2022)
$script:vcDir = $null
foreach ($vc in @("vc17", "vc16", "vc15")) {
    if (Test-Path (Join-Path $OCV_ROOT "build\x64\$vc\lib")) {
        $script:vcDir = $vc
        Write-Host ("  [INFO] OpenCV binaries found: x64\" + $vc) -ForegroundColor Gray
        break
    }
}
if (-not $script:vcDir) {
    Write-Host "  [WARN] Could not find x64\vc16 or vc17 -- check extraction." -ForegroundColor Yellow
}

# ==============================================================================
#  2.  ONNX Runtime 1.18.1  (CPU build, ~9 MB)
#
#  For GPU/CUDA inference instead, download the GPU package:
#    https://github.com/microsoft/onnxruntime/releases/download/v1.18.1/onnxruntime-win-x64-gpu-cuda12-1.18.1.zip
#  Extract and replace the contents of deps\onnxruntime\
# ==============================================================================
Write-Host ""
Write-Host "[2/2]  ONNX Runtime 1.18.1 (CPU)" -ForegroundColor Cyan
Write-Host "  (For GPU/CUDA: see comment in setup.ps1)" -ForegroundColor Gray

$ORT_VER  = "1.18.1"
$ORT_ZIP  = Join-Path $DownloadDir "onnxruntime-win-x64-$ORT_VER.zip"
$ORT_TMP  = Join-Path $DepsDir     "onnxruntime-win-x64-$ORT_VER"
$ORT_ROOT = Join-Path $DepsDir     "onnxruntime"
$ORT_URL  = "https://github.com/microsoft/onnxruntime/releases/download/v$ORT_VER/onnxruntime-win-x64-$ORT_VER.zip"

Get-Dep -Url $ORT_URL -Out $ORT_ZIP -Label "ONNX Runtime $ORT_VER"

if (-not (Test-Path (Join-Path $ORT_ROOT "include\onnxruntime_cxx_api.h"))) {
    Write-Host "  [EXTR] Extracting ONNX Runtime..." -ForegroundColor White
    Expand-Archive -Path $ORT_ZIP -DestinationPath $DepsDir -Force
    # The zip extracts to onnxruntime-win-x64-1.18.1\ -- rename to onnxruntime\
    if (Test-Path $ORT_TMP) {
        if (Test-Path $ORT_ROOT) { Remove-Item $ORT_ROOT -Recurse -Force }
        Rename-Item -Path $ORT_TMP -NewName "onnxruntime"
    }
    Write-Host "  [OK  ] ONNX Runtime extracted." -ForegroundColor Green
} else {
    Write-Host "  [SKIP] ONNX Runtime already extracted." -ForegroundColor Yellow
}

# ==============================================================================
#  Summary
# ==============================================================================
$vc = if ($script:vcDir) { $script:vcDir } else { "vc16" }

Write-Host ""
Write-Host "======================================================" -ForegroundColor Green
Write-Host "  Setup complete!" -ForegroundColor Green
Write-Host "======================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Dependency layout:" -ForegroundColor White
Write-Host "  deps\" -ForegroundColor Gray
Write-Host ("  +-- opencv\build\include\         (headers)") -ForegroundColor Gray
Write-Host ("  +-- opencv\build\x64\" + $vc + "\lib\   (libs)") -ForegroundColor Gray
Write-Host ("  +-- opencv\build\x64\" + $vc + "\bin\   (DLLs)") -ForegroundColor Gray
Write-Host ("  +-- onnxruntime\include\          (headers)") -ForegroundColor Gray
Write-Host ("  +-- onnxruntime\lib\              (lib + DLLs)") -ForegroundColor Gray
Write-Host ""
Write-Host "Next steps:" -ForegroundColor White
Write-Host "  1. Copy best.onnx to the project root" -ForegroundColor White
Write-Host "  2. Open YOLOv8_Overlay_CS2.sln in Visual Studio 2022" -ForegroundColor White
Write-Host "  3. Select Release | x64 and press Ctrl+Shift+B" -ForegroundColor White
Write-Host "  4. Run x64\Release\YOLOv8_Overlay_CS2.exe" -ForegroundColor White
Write-Host ""

if ($script:vcDir -and $script:vcDir -ne "vc16") {
    Write-Host ("  [NOTE] OpenCV uses " + $script:vcDir + ". The .vcxproj includes both vc16 and vc17 lib paths.") -ForegroundColor Yellow
}
