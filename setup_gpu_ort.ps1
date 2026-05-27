# setup_gpu_ort.ps1
# Install ONNX Runtime 1.20.1 GPU (CUDA 12, cuDNN 9.x)
# Compatible with PyTorch 2.6.0 (cudnn64_9.dll)
# Run ONCE, then: 1) recompile VS  2) run copy_cuda_dlls.ps1  3) launch exe

$ErrorActionPreference = "Stop"
$ProgressPreference    = "SilentlyContinue"

$ProjectRoot = $PSScriptRoot
$DepsDir     = Join-Path $ProjectRoot "deps"
$DlDir       = Join-Path $DepsDir "_downloads"
New-Item -ItemType Directory -Force -Path $DlDir | Out-Null

Write-Host ""
Write-Host "================================================" -ForegroundColor Cyan
Write-Host "   ONNX Runtime 1.20.1 GPU (CUDA 12 / cuDNN 9)" -ForegroundColor Cyan
Write-Host "================================================" -ForegroundColor Cyan
Write-Host ""

# ---- GPU check ---------------------------------------------------------------
Write-Host "[0/3] GPU check..." -ForegroundColor Cyan
$smi = "C:\Windows\System32\nvidia-smi.exe"
if (Test-Path $smi) {
    $info = & $smi "--query-gpu=name,driver_version" "--format=csv,noheader" 2>$null
    if ($info) {
        Write-Host ("  [OK ] " + $info) -ForegroundColor Green
    }
} else {
    Write-Host "  [WARN] nvidia-smi not found." -ForegroundColor Yellow
}

# ---- Download ORT GPU 1.20.1 -------------------------------------------------
Write-Host ""
Write-Host "[1/3] Download ONNX Runtime 1.20.1 GPU..." -ForegroundColor Cyan

$VER     = "1.20.1"
# Try the standard GPU package name first (1.19+ naming)
$ZIPNAME = "onnxruntime-win-x64-gpu-" + $VER + ".zip"
$ZIP     = Join-Path $DlDir $ZIPNAME
$URL     = "https://github.com/microsoft/onnxruntime/releases/download/v" + $VER + "/" + $ZIPNAME

if (Test-Path $ZIP) {
    $sz = [math]::Round((Get-Item $ZIP).Length / 1MB, 1)
    Write-Host ("  [SKIP] Already present (" + $sz + " MB)") -ForegroundColor Yellow
} else {
    Write-Host ("  [DL  ] " + $URL) -ForegroundColor White
    try {
        Invoke-WebRequest -Uri $URL -OutFile $ZIP -UseBasicParsing
        $sz = [math]::Round((Get-Item $ZIP).Length / 1MB, 1)
        Write-Host ("  [OK  ] Downloaded (" + $sz + " MB)") -ForegroundColor Green
    } catch {
        Write-Host ("  [FAIL] " + $_.Exception.Message) -ForegroundColor Red
        Write-Host ""
        Write-Host "  Download manually from:" -ForegroundColor Yellow
        Write-Host ("  " + $URL) -ForegroundColor Yellow
        Write-Host ("  Save to: " + $ZIP) -ForegroundColor Yellow
        Write-Host ""
        Write-Host "  Or check latest releases at:" -ForegroundColor Yellow
        Write-Host "  https://github.com/microsoft/onnxruntime/releases" -ForegroundColor Yellow
        exit 1
    }
}

# ---- Extract and replace deps\onnxruntime ------------------------------------
Write-Host ""
Write-Host "[2/3] Replacing deps\onnxruntime..." -ForegroundColor Cyan

$ORT_ROOT = Join-Path $DepsDir "onnxruntime"

if (Test-Path $ORT_ROOT) {
    Write-Host "  [RM  ] Removing old ORT..." -ForegroundColor White
    Remove-Item $ORT_ROOT -Recurse -Force
}

Write-Host "  [EXTR] Extracting..." -ForegroundColor White
Expand-Archive -Path $ZIP -DestinationPath $DepsDir -Force

# Rename extracted folder to "onnxruntime"
$extracted = Get-ChildItem $DepsDir -Directory |
             Where-Object { $_.Name -like "onnxruntime-win-x64-gpu*" } |
             Select-Object -First 1

if ($null -eq $extracted) {
    Write-Host "  [ERR ] Extracted folder not found in $DepsDir" -ForegroundColor Red
    exit 1
}

Rename-Item -Path $extracted.FullName -NewName "onnxruntime"
Write-Host "  [OK  ] Extracted to deps\onnxruntime\" -ForegroundColor Green

# ---- Copy provider DLLs to x64\Release\ --------------------------------------
Write-Host ""
Write-Host "[3/3] Copying ORT DLLs to x64\Release..." -ForegroundColor Cyan

$LibDir     = Join-Path $ORT_ROOT "lib"
$ReleaseDir = Join-Path $ProjectRoot "x64\Release"

if (-not (Test-Path $ReleaseDir)) {
    Write-Host "  [WARN] x64\Release not found -- compile first, then re-run this script." -ForegroundColor Yellow
} else {
    $dllsToCopy = @(
        "onnxruntime.dll",
        "onnxruntime_providers_shared.dll",
        "onnxruntime_providers_cuda.dll",
        "onnxruntime_providers_tensorrt.dll"
    )
    foreach ($dll in $dllsToCopy) {
        $src = Join-Path $LibDir $dll
        $dst = Join-Path $ReleaseDir $dll
        if (Test-Path $src) {
            Copy-Item $src $dst -Force
            $sz = [math]::Round((Get-Item $src).Length / 1MB, 1)
            Write-Host ("  [COPY] " + $dll + " (" + $sz + " MB)") -ForegroundColor Green
        } else {
            Write-Host ("  [SKIP] " + $dll + " not found in lib\") -ForegroundColor Yellow
        }
    }
}

# ---- Check what we have ------------------------------------------------------
Write-Host ""
Write-Host "  DLLs in deps\onnxruntime\lib:" -ForegroundColor White
$checkDlls = @("onnxruntime.dll","onnxruntime_providers_shared.dll","onnxruntime_providers_cuda.dll")
foreach ($dll in $checkDlls) {
    $p = Join-Path $LibDir $dll
    if (Test-Path $p) {
        $sz = [math]::Round((Get-Item $p).Length / 1MB, 1)
        Write-Host ("    [OK ] " + $dll + " (" + $sz + " MB)") -ForegroundColor Green
    } else {
        Write-Host ("    [MISS] " + $dll) -ForegroundColor Red
    }
}

# ---- Summary -----------------------------------------------------------------
Write-Host ""
Write-Host "================================================" -ForegroundColor Green
Write-Host "  ORT 1.20.1 GPU installed!" -ForegroundColor Green
Write-Host "================================================" -ForegroundColor Green
Write-Host ""
Write-Host "NEXT STEPS (in order):" -ForegroundColor White
Write-Host "  1. Visual Studio -> Clean Solution (Build menu)" -ForegroundColor White
Write-Host "  2. Visual Studio -> Rebuild Solution (Ctrl+Alt+F7)" -ForegroundColor White
Write-Host "  3. Run:  .\copy_cuda_dlls.ps1" -ForegroundColor Cyan
Write-Host "     (copies cuDNN 9.x DLLs from PyTorch -- now version-compatible!)" -ForegroundColor Gray
Write-Host "  4. Launch x64\Release\YOLOv8_Overlay_CS2.exe" -ForegroundColor White
Write-Host "  Expected: INF: GPU 4-10ms  -> 120-200 FPS" -ForegroundColor Green
Write-Host ""
