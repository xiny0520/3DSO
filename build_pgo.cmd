@echo off
setlocal

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "BUILD_DIR=%ROOT%\build_pgo_vs"
set "CMAKE_EXE=cmake"
set "VS_GENERATOR=Visual Studio 17 2022"
if exist "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    set "VS_GENERATOR=Visual Studio 18 2026"
)
if exist "C:\Program Files\Microsoft Visual Studio\17\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\17\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    set "VS_GENERATOR=Visual Studio 17 2022"
)
set "BENCH_PS1=%ROOT%\benchmark_3dso.ps1"
set "EXE_PATH=%BUILD_DIR%\Release\3dso.exe"
set "VC_BIN_DIR="

for /f "usebackq delims=" %%D in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$roots=@('C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC','C:\Program Files\Microsoft Visual Studio\17\Community\VC\Tools\MSVC'); foreach($root in $roots){ if(Test-Path $root){ $dir=Get-ChildItem $root -Directory | Sort-Object Name -Descending | Select-Object -First 1; if($dir){ Join-Path $dir.FullName 'bin\Hostx64\x64'; break } } }"`) do (
    set "VC_BIN_DIR=%%D"
)

if not defined VC_BIN_DIR (
    echo Failed to locate MSVC runtime tools directory for PGO.
    exit /b 1
)

echo [1/3] Configuring and building instrumented PGO binary...
if exist "%BUILD_DIR%\Release\3dso!*.pgc" del /q "%BUILD_DIR%\Release\3dso!*.pgc" >nul 2>nul
if exist "%BUILD_DIR%\Release\3dso.pgd" del /q "%BUILD_DIR%\Release\3dso.pgd" >nul 2>nul
"%CMAKE_EXE%" -S "%ROOT%" -B "%BUILD_DIR%" -G "%VS_GENERATOR%" -A x64 -DDSO_ENABLE_IPO=ON -DDSO_ENABLE_AVX2=ON -DDSO_PGO_MODE=INSTRUMENT
if errorlevel 1 exit /b 1

"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release
if errorlevel 1 exit /b 1

echo [2/3] Running benchmark dataset to generate PGO profile data...
set "PATH=%VC_BIN_DIR%;%PATH%"
powershell -ExecutionPolicy Bypass -File "%BENCH_PS1%" -Executable "%EXE_PATH%" -Repeats 3 -Threads 8
if errorlevel 1 exit /b 1

echo [3/3] Rebuilding optimized PGO binary...
"%CMAKE_EXE%" -S "%ROOT%" -B "%BUILD_DIR%" -G "%VS_GENERATOR%" -A x64 -DDSO_ENABLE_IPO=ON -DDSO_ENABLE_AVX2=ON -DDSO_PGO_MODE=OPTIMIZE
if errorlevel 1 exit /b 1

"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release
if errorlevel 1 exit /b 1

echo PGO build finished: %BUILD_DIR%

