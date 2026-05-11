# 3DSO

3DSO is a C++20 command-line tool for calculating 3D structural organization metrics from point-cloud plot files.

The repository is kept as a compact source-code release.

## Repository Structure

```text
include/        Public C++ headers
src/            C++ source files
CMakeLists.txt  CMake build script
LICENSE         License file
```

## Build

Requirements:

- CMake 3.21 or newer
- A C++20 compiler
- OpenMP support is optional, but recommended

### Windows

Install Visual Studio 2022 or newer with the "Desktop development with C++" workload, then run:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

The executable will be generated at:

```text
build/Release/3dso.exe
```

### Linux / macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executable will be generated at:

```text
build/3dso
```

## Run

```bash
3dso --input-dir path/to/input --output-csv result.csv
```

Common options:

```text
--input-dir DIR        Input directory
--output-csv FILE      Output CSV file
--k-voxel INT          Block edge length in voxels, usually 3
--voxel-size FLOAT     Voxel size in meters
--block-ratio INT      Spatial grid multiplier
--plot-size MODE X Y   Plot size mode and dimensions
--threads INT          Number of OpenMP threads
```

Run `3dso --help` to view the full option list.

## Input

The program accepts `.txt`, `.xyz`, and `.las` point-cloud files. Text files should contain at least three numeric columns:

```text
x y z
```

## License

MIT License. See [LICENSE](LICENSE).
