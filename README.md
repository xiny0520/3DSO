# 3DSO

3DSO is a C++20 command-line implementation of 3DSO calculation for 3D point-cloud plots. It is designed for reproducible research workflows where numerical stability, clear input/output behavior, and benchmark transparency matter more than unchecked peak speed.

The tool reads `.las`, `.xyz`, and `.txt` point-cloud files, voxelizes each plot, extracts canonical 3D block patterns, estimates spatial entropy, and writes one CSV row per input file.

## Current Status

- Stable default path for `.las`, `.xyz`, and `.txt` input.
- Release-oriented CMake project with a reusable `3dso_core` target and a thin CLI executable.
- Default `.las` path is optimized for the common `k_voxel = 3` case while preserving conservative float-coordinate voxelization semantics.
- Conservative fallback path remains available with `--disable-direct-packed-las`.
- Regression scripts verify CLI behavior and output parity between the fast LAS path and the conservative path.
- Large benchmark data is intentionally not tracked by Git.

## Repository Layout

```text
include/tdso/tdso.hpp          Public C++ entry point
include/tdso/results.hpp       Result data structures shared by the core and output writer
src/main.cpp                   CLI main function
src/cli_args.cpp               CLI argument parsing and usage text
src/csv_writer.cpp             CSV header, escaping, and row writing
src/tdso_engine.cpp            3DSO implementation
docs/architecture.md           Data flow and implementation notes
docs/build.md                  Reproducible build and release guide
docs/reliability.md            Stability and regression policy
scripts/compare_fast_path.ps1  LAS fast-path parity check
scripts/build.ps1              Cross-generator PowerShell build helper
tests/run_regression.ps1       Lightweight regression test suite
tests/README.md                Test usage notes
data/sample_plot.txt           Small text point-cloud fixture
benchmark_3dso.ps1             Benchmark helper
build_3dso.cmd                 Windows Release build helper
build_pgo.cmd                  Windows MSVC PGO build helper
python_example.py              Python subprocess example
run_3dso_from_txt.py           Helper for one text point-cloud file
data/BENCHMARK_DATASET.md      Benchmark data layout notes
```

## Quick Start

Build and run the regression checks on Windows:

```powershell
.\build_3dso.cmd -RunTests
```

Or build manually with CMake:

```powershell
cmake --preset release
cmake --build --preset release
```

If your CMake version does not support presets, use the equivalent explicit command:

```powershell
cmake -S . -B build_release `
  -DCMAKE_BUILD_TYPE=Release `
  -DDSO_ENABLE_IPO=ON `
  -DDSO_ENABLE_AVX2=ON `
  -DDSO_ENABLE_NATIVE=OFF `
  -DDSO_ENABLE_FAST_MATH=OFF

cmake --build build_release --config Release
```

Run the bundled sample data:

```powershell
.\build_release\Release\3dso.exe `
  --input-dir .\data `
  --output-csv .\results\sample.csv `
  --k-voxel 3 `
  --voxel-size 0.3 `
  --block-ratio 5 `
  --plot-size 1 2 2 `
  --threads 1 `
  --limit 1
```

With single-config generators such as Ninja or Makefiles, the executable is usually `build_release\3dso.exe` on Windows or `build_release/3dso` on Linux.

See [docs/build.md](docs/build.md) for the complete build, validation, and release checklist.

## Algorithm Overview

For each input file, the CLI performs the following steps:

1. Read point coordinates from `.las`, `.xyz`, or `.txt`.
2. Determine plot extent from either `--plot-size` or the point bounds.
3. Convert the point cloud to a binary occupancy voxel grid.
4. Scan non-overlapping `k_voxel x k_voxel x k_voxel` blocks.
5. Canonicalize each occupied block under the supported rotation maps.
6. Count canonical patterns and their spatial distribution.
7. Compute 3DSO-related terms, including `Iw_total`, `Ib_total`, `Ic_total`, and normalized spatial entropy.
8. Write one CSV row per source file.

The common `k_voxel = 3` case uses compact 64-bit block fingerprints. For LAS input, the default fast path writes directly to packed z-column voxel storage, but it preserves the same float-coordinate behavior as the conservative path.

## Build

### Requirements

- CMake 3.21 or newer.
- A C++20 compiler.
- Optional: OpenMP for file-level parallelism.
- Optional: MSVC with Visual Studio on Windows for the provided `.cmd` build helpers.

### Windows Helper Build

```powershell
.\build_3dso.cmd
```

This creates a Visual Studio Release build in:

```text
build_release_vs/
```

The executable is usually:

```text
build_release_vs/Release/3dso.exe
```

### Manual CMake Build

```powershell
cmake -S . -B build_release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DDSO_ENABLE_IPO=ON `
  -DDSO_ENABLE_AVX2=ON

cmake --build build_release --config Release
```

### CMake Options

```text
DSO_ENABLE_IPO=ON          Enable IPO/LTO when supported.
DSO_ENABLE_AVX2=ON         Enable AVX2-capable code generation.
DSO_ENABLE_NATIVE=OFF      Enable host-specific tuning for local benchmarks.
DSO_ENABLE_FAST_MATH=OFF   Enable aggressive floating-point optimizations for benchmark-only builds.
DSO_PGO_MODE=OFF           MSVC PGO mode: OFF, INSTRUMENT, or OPTIMIZE.
```

For portable release binaries and reproducible paper runs, keep:

```text
DSO_ENABLE_NATIVE=OFF
DSO_ENABLE_FAST_MATH=OFF
```

Use `DSO_ENABLE_NATIVE=ON` or `DSO_ENABLE_FAST_MATH=ON` only for local machine-specific benchmarking, and report those options with timing results.

## Command-Line Usage

```powershell
.\build_release_vs\Release\3dso.exe `
  --input-dir .\data `
  --output-csv .\results\3DSO_results.csv `
  --k-voxel 3 `
  --voxel-size 0.3 `
  --block-ratio 5 `
  --plot-size 1 25 25 `
  --threads 8
```

### Input Files

The input directory is scanned for:

```text
.las
.xyz
.txt
```

Text input is expected to contain at least three numeric columns representing:

```text
x y z
```

Whitespace and comma separators are accepted by the fast text parser.

### CLI Options

```text
--input-dir DIR
    Directory containing .las, .xyz, or .txt files.

--output-csv FILE
    Destination CSV file.

--k-voxel INT
    Block edge length in voxels. Default: 3.

--voxel-size FLOAT
    Voxel size in meters. Default: 0.3.

--block-ratio INT
    Spatial entropy grid size multiplier relative to block length. Default: 5.

--plot-size MODE X Y
    Plot extent mode.
    MODE 0 = infer X/Y extent from point bounds.
    MODE 1 = use manual X/Y extent.
    Default: 1 25 25.

--layers INT
    Split the height range into N layers and append layer-level outputs.
    0 disables layer output.

--threads INT
    OpenMP thread count.
    0 uses the OpenMP default/max thread count.

--limit INT
    Limit the number of input files processed after sorting.
    0 means all files.

--verbose
    Print per-file status lines.

--profile-phases
    Print aggregate timing for read, voxelize, scan, reduce, and layer phases.

--disable-direct-packed-las
    Disable the default exact packed LAS fast path and use the conservative path.

--experimental-packed-columns
    Force packed z-column storage on non-default paths for k=3 blocks.

--experimental-direct-las
    Experimental direct LAS voxelization path. Not part of the stable default.
```

## CSV Output

The output CSV includes:

```text
PlotID
SourceFile
3DSO
3DSO_raw
Iw_total
Ib_total
Ic_total
H_sp_Global
H_sp_Global_norm
NumPatterns
TotalBlocks
HR98
BaseArea
Nx
Ny
Nz
NumPoints
Status
```

When `--layers N` is enabled, additional columns are appended:

```text
3DSO_L1 ... 3DSO_LN
Ic_L1 ... Ic_LN
Hsp_L1 ... Hsp_LN
Npts_L1 ... Npts_LN
```

Common `Status` values:

```text
ok
empty_input
invalid_plot_range
no_valid_points
no_patterns
```

## Reliability Policy

This project prioritizes:

```text
1. Safety
2. Reproducibility
3. Stable numerical behavior
4. Speed
```

The default LAS fast path is allowed only because it has been checked against the conservative path and preserves CSV output parity on the benchmark set. The conservative path remains available:

```powershell
.\build_release_vs\Release\3dso.exe `
  --input-dir .\data `
  --output-csv .\results\conservative.csv `
  --disable-direct-packed-las
```

Experimental options should not be used for published results unless their output parity has been verified for the target dataset.

See [docs/reliability.md](docs/reliability.md) for the full policy.

## Regression Tests

Run the lightweight regression suite:

```powershell
.\tests\run_regression.ps1 `
  -Executable .\build_release_vs\Release\3dso.exe `
  -DataDir .\data `
  -LasLimit 10 `
  -Threads 1
```

The regression suite checks:

- `--help` exits successfully.
- Missing values and unknown CLI options fail with clear errors.
- Invalid numeric arguments fail.
- Empty input directories are handled without crashing.
- A small text point-cloud input produces a valid CSV row.
- CSV output preserves filenames containing commas or quotes.
- Layer output keeps a stable column shape for error rows.
- If LAS data is available, the default fast path is compared against `--disable-direct-packed-las`.

The repository also includes a GitHub Actions workflow that builds on Windows and Linux and runs the lightweight checks where supported.

For release validation, compare all LAS files:

```powershell
.\tests\run_regression.ps1 `
  -Executable .\build_release_vs\Release\3dso.exe `
  -DataDir .\data `
  -LasLimit 0 `
  -Threads 8
```

You can run only the LAS fast-path parity check:

```powershell
.\scripts\compare_fast_path.ps1 `
  -Executable .\build_release_vs\Release\3dso.exe `
  -InputDir .\data `
  -OutputDir .\build_fast_path_check `
  -Limit 0 `
  -Threads 8 `
  -Tolerance 1e-9
```

## Benchmarking

Run the benchmark helper:

```powershell
.\benchmark_3dso.ps1 -Threads 8 -Repeats 3
```

Outputs are written to:

```text
benchmark_runs/
```

Useful benchmark options:

```powershell
.\benchmark_3dso.ps1 -Threads 16 -Repeats 5
.\benchmark_3dso.ps1 -SkipWarmup -Threads 8
```

When reporting timing results, record:

- CPU model.
- Operating system.
- Compiler and compiler version.
- CMake options.
- Thread count.
- Dataset name/version/checksum.
- Git commit hash.

## PGO Build On Windows

MSVC profile-guided optimization is available through:

```powershell
.\build_pgo.cmd
```

The script performs:

1. Build an instrumented Release binary.
2. Run the benchmark dataset to collect profile data.
3. Rebuild an optimized PGO binary.

PGO can improve local benchmark speed, but it should be documented clearly when used for published timing numbers.

## Data And GitHub Usage

Large LAS files are ignored by default:

```text
data/*.las
```

This keeps the repository lightweight and avoids accidentally publishing restricted or oversized data. Recommended public repository contents include:

```text
CMakeLists.txt
README.md
.clang-format
.gitignore
include/
src/
docs/
scripts/
tests/
benchmark_3dso.ps1
build_3dso.cmd
build_pgo.cmd
python_example.py
run_3dso_from_txt.py
data/BENCHMARK_DATASET.md
```

Do not commit:

```text
build/
build_*/
build_release/
build_release_vs/
build_github_check/
benchmark_runs/
.refactor_backup/
__pycache__/
data/*.las
*.csv
```

## Python Helpers

`python_example.py` demonstrates how to call the CLI from Python:

```powershell
python .\python_example.py
```

`run_3dso_from_txt.py` runs the CLI for a single `.txt` or `.xyz` file by creating a temporary one-file input directory:

```powershell
python .\run_3dso_from_txt.py path\to\plot.txt `
  --executable .\build_release_vs\Release\3dso.exe `
  --output-csv .\results\single_file.csv
```

These helpers use subprocess calls. They are not Python bindings.

## Development Guidelines

Before accepting a code change:

1. Build in Release mode.
2. Run `tests/run_regression.ps1`.
3. Run `scripts/compare_fast_path.ps1` on representative LAS data.
4. Confirm no unexpected CSV changes.
5. Benchmark only after correctness checks pass.

Optimization changes should keep a conservative fallback path unless the new implementation is strictly a refactor with proven output parity.

## Known Boundaries

- The default fast path is strongest for LAS input with `k_voxel = 3`.
- `--layers` intentionally uses a more conservative path.
- The text reader is optimized for simple numeric point files, not arbitrary CSV dialects.
- LAS support focuses on coordinates needed for 3DSO calculation.

## License

This project is distributed under the MIT License. See [LICENSE](LICENSE).




