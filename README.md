# 3DSO

3DSO is a C++20 command-line tool for calculating 3D structural organization metrics from point-cloud plots.

It reads `.las`, `.xyz`, and `.txt` files, voxelizes each plot, extracts canonical 3D block patterns, and writes one CSV row per input file.

## Environment

To build the executable from source, prepare a C++ build environment first.

On Windows, install:

- Visual Studio 2022 or newer
- The "Desktop development with C++" workload
- CMake tools, included with Visual Studio or installed separately

Then open PowerShell in the repository root and run:

```powershell
.\build_3dso.cmd -RunTests
```

This creates:

```text
build_release_vs/Release/3dso.exe
```

On Linux or other platforms, use any C++20 compiler with CMake 3.21+:

```powershell
cmake --preset release
cmake --build --preset release
```

See [docs/build.md](docs/build.md) for the detailed build guide.

## Quick Start

Run the bundled sample:

```powershell
.\build_release_vs\Release\3dso.exe `
  --input-dir .\data `
  --output-csv .\results\sample.csv `
  --k-voxel 3 `
  --voxel-size 0.1 `
  --block-ratio 5 `
  --plot-size 1 2 2 `
  --threads 1 `
  --limit 1
```

## Input

The input directory may contain:

```text
.las
.xyz
.txt
```

Text input should contain at least three numeric columns:

```text
x y z
```

Whitespace and comma separators are accepted.

## Main Options

```text
--input-dir DIR        Input directory
--output-csv FILE      Output CSV path
--k-voxel INT          Block edge length in voxels, default 3
--voxel-size FLOAT     Voxel size in meters, default 0.1
--block-ratio INT      Spatial grid multiplier, default 5
--plot-size MODE X Y   MODE 0=auto, MODE 1=manual plot size
--layers INT           Optional height-layer outputs
--threads INT          OpenMP thread count, 0=default/max
--limit INT            Optional file-count limit
```

Run `3dso --help` for the complete option list.

## Output

The CSV contains:

```text
PlotID
SourceFile
3DSO
3DSO_raw
Iw_total
Ib_total
Ic_total
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

When `--layers N` is enabled, layer-level columns are appended:

```text
3DSO_L1 ... 3DSO_LN
Ic_L1 ... Ic_LN
Hsp_L1 ... Hsp_LN
Npts_L1 ... Npts_LN
```

Common `Status` values are `ok`, `empty_input`, `invalid_plot_range`, `no_valid_points`, and `no_patterns`.

## Validation

Run the regression checks:

```powershell
.\tests\run_regression.ps1 `
  -Executable .\build_release_vs\Release\3dso.exe `
  -DataDir .\data `
  -Threads 1
```

If representative LAS files are available, compare the default LAS fast path with the conservative path:

```powershell
.\scripts\compare_fast_path.ps1 `
  -Executable .\build_release_vs\Release\3dso.exe `
  -InputDir .\data `
  -OutputDir .\build_fast_path_check `
  -Limit 0 `
  -Threads 8
```

## Notes For Reproducible Use

For paper results and portable binaries, keep:

```text
DSO_ENABLE_NATIVE=OFF
DSO_ENABLE_FAST_MATH=OFF
```

Large LAS datasets are not tracked by Git. Put benchmark data under `data/` locally and document the dataset version or checksum when reporting results.

More details:

- [Architecture](docs/architecture.md)
- [Build guide](docs/build.md)
- [Reliability policy](docs/reliability.md)
- [Test notes](tests/README.md)

## License

MIT License. See [LICENSE](LICENSE).
