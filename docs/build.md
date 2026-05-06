# Build And Release Guide

This project provides three supported build modes:

1. Portable Release: use this for paper results and public binaries.
2. Debug: use this for development.
3. Native Benchmark Release: use this only for local performance experiments.

The default build favors reproducible numerical behavior. Benchmark-only options such as fast math and native CPU tuning must be enabled explicitly.

## Requirements

- CMake 3.21 or newer.
- A C++20 compiler.
- Optional but recommended: OpenMP.
- Windows: Visual Studio 2022 or newer with "Desktop development with C++" and CMake tools.
- Linux: GCC 11+, Clang 14+, or another C++20-capable compiler.

## One-Command Windows Build

From the repository root:

```powershell
.\build_3dso.cmd -RunTests
```

This command:

1. Locates CMake from PATH or the Visual Studio installation.
2. Configures a 64-bit Release build in `build_release_vs/`.
3. Builds `3dso.exe`.
4. Runs the lightweight regression suite when `-RunTests` is provided.

The executable is written to:

```text
build_release_vs/Release/3dso.exe
```

Equivalent PowerShell entry point:

```powershell
.\scripts\build.ps1 -BuildDir .\build_release_vs -Config Release -RunTests
```

## Manual CMake Build

The portable Release configuration is:

```powershell
cmake -S . -B build_release `
  -DCMAKE_BUILD_TYPE=Release `
  -DDSO_ENABLE_IPO=ON `
  -DDSO_ENABLE_AVX2=ON `
  -DDSO_ENABLE_NATIVE=OFF `
  -DDSO_ENABLE_FAST_MATH=OFF

cmake --build build_release --config Release
```

With CMake presets:

```powershell
cmake --preset release
cmake --build --preset release
```

On Linux, the executable is usually:

```text
build_release/3dso
```

On Windows multi-configuration generators, it is usually:

```text
build_release/Release/3dso.exe
```

## Validation

Run the regression suite after every source or build-system change:

```powershell
.\tests\run_regression.ps1 `
  -Executable .\build_release_vs\Release\3dso.exe `
  -DataDir .\data `
  -Threads 1
```

If representative `.las` files are present in `data/`, validate the default LAS fast path against the conservative path:

```powershell
.\scripts\compare_fast_path.ps1 `
  -Executable .\build_release_vs\Release\3dso.exe `
  -InputDir .\data `
  -OutputDir .\build_fast_path_check `
  -Limit 0 `
  -Threads 8 `
  -Tolerance 1e-9
```

## Build Options

```text
DSO_ENABLE_IPO=ON          Enable interprocedural optimization when supported.
DSO_ENABLE_AVX2=ON         Enable AVX2 code generation for the core target.
DSO_ENABLE_NATIVE=OFF      Keep binaries portable across machines.
DSO_ENABLE_FAST_MATH=OFF   Keep conservative floating-point behavior.
DSO_PGO_MODE=OFF           MSVC PGO mode: OFF, INSTRUMENT, or OPTIMIZE.
```

For paper results and public binaries, keep:

```text
DSO_ENABLE_NATIVE=OFF
DSO_ENABLE_FAST_MATH=OFF
```

For local benchmark-only builds:

```powershell
cmake --preset benchmark-native
cmake --build --preset benchmark-native
```

Benchmark builds may use host-specific CPU instructions and aggressive floating-point transformations. Report those options explicitly when publishing timing numbers.

## Release Checklist

Before tagging or uploading a release:

1. Build Portable Release.
2. Run `tests/run_regression.ps1`.
3. Run `scripts/compare_fast_path.ps1` on representative LAS data.
4. Save compiler version, CMake version, OS, CPU, thread count, and CMake options.
5. Record dataset name/version/checksum for benchmark results.
6. Confirm generated CSV files are not committed.

This checklist is intentionally small. A reader should be able to reproduce the build and understand which performance options were used.


