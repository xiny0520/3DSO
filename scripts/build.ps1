param(
    [string]$BuildDir = "",
    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string]$Config = "Release",
    [string]$Generator = "",
    [switch]$RunTests,
    [switch]$Clean,
    [switch]$EnableFastMath,
    [switch]$EnableNative,
    [switch]$DisableIPO,
    [switch]$DisableAVX2
)

$ErrorActionPreference = "Stop"
$ScriptRoot = Split-Path -Parent $PSCommandPath
$ProjectRoot = Split-Path -Parent $ScriptRoot

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $ProjectRoot "build_release_vs"
}
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

function Find-CMake {
    $fromPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    $candidates = @(
        "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\17\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\17\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\17\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw "CMake was not found. Install CMake 3.21+ or Visual Studio with the C++ CMake tools."
}

function Find-VisualStudioGenerator {
    $vs18 = @(
        "C:\Program Files\Microsoft Visual Studio\18\Community",
        "C:\Program Files\Microsoft Visual Studio\18\Professional",
        "C:\Program Files\Microsoft Visual Studio\18\Enterprise"
    )
    foreach ($path in $vs18) {
        if (Test-Path -LiteralPath $path) {
            return "Visual Studio 18 2026"
        }
    }

    $vs17 = @(
        "C:\Program Files\Microsoft Visual Studio\17\Community",
        "C:\Program Files\Microsoft Visual Studio\17\Professional",
        "C:\Program Files\Microsoft Visual Studio\17\Enterprise"
    )
    foreach ($path in $vs17) {
        if (Test-Path -LiteralPath $path) {
            return "Visual Studio 17 2022"
        }
    }

    return ""
}

function ConvertTo-CMakeBool {
    param([bool]$Value)
    if ($Value) {
        return "ON"
    }
    return "OFF"
}

$cmake = Find-CMake
if ([string]::IsNullOrWhiteSpace($Generator)) {
    $Generator = Find-VisualStudioGenerator
}

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    $projectRootFull = [System.IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\', '/')
    $buildDirFull = [System.IO.Path]::GetFullPath($BuildDir).TrimEnd('\', '/')
    if (-not $buildDirFull.StartsWith($projectRootFull + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean a build directory outside the project root: $BuildDir"
    }
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

$configureArgs = @(
    "-S", $ProjectRoot,
    "-B", $BuildDir,
    "-DDSO_ENABLE_IPO=$(ConvertTo-CMakeBool -Value (-not $DisableIPO.IsPresent))",
    "-DDSO_ENABLE_AVX2=$(ConvertTo-CMakeBool -Value (-not $DisableAVX2.IsPresent))",
    "-DDSO_ENABLE_NATIVE=$(ConvertTo-CMakeBool -Value $EnableNative.IsPresent)",
    "-DDSO_ENABLE_FAST_MATH=$(ConvertTo-CMakeBool -Value $EnableFastMath.IsPresent)"
)

if (-not [string]::IsNullOrWhiteSpace($Generator)) {
    $configureArgs += @("-G", $Generator)
    if ($Generator -like "Visual Studio*") {
        $configureArgs += @("-A", "x64")
    } else {
        $configureArgs += "-DCMAKE_BUILD_TYPE=$Config"
    }
} else {
    $configureArgs += "-DCMAKE_BUILD_TYPE=$Config"
}

Write-Host "[configure] $cmake $($configureArgs -join ' ')"
& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE"
}

Write-Host "[build] $cmake --build $BuildDir --config $Config"
& $cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE"
}

$isWindowsHost = ($env:OS -eq "Windows_NT")
$exeName = if ($isWindowsHost) { "3dso.exe" } else { "3dso" }
$exePath = Join-Path $BuildDir $exeName
if (-not (Test-Path -LiteralPath $exePath)) {
    $exePath = Join-Path (Join-Path $BuildDir $Config) $exeName
}
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Build completed, but the executable was not found under $BuildDir"
}

Write-Host "[ok] executable: $exePath"

if ($RunTests) {
    $regression = Join-Path $ProjectRoot "tests\run_regression.ps1"
    Write-Host "[test] $regression"
    & $regression -Executable $exePath -DataDir (Join-Path $ProjectRoot "data") -Threads 1
    if ($LASTEXITCODE -ne 0) {
        throw "Regression tests failed with exit code $LASTEXITCODE"
    }
}
