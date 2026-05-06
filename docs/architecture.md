# 3DSO Architecture

This project is organized as a small command-line application backed by a reusable C++ core.

## Targets

- `3dso_core`: static library containing the 3DSO implementation.
- `3dso`: CLI executable with a thin `main` function.

## Data Flow

1. Discover `.xyz`, `.txt`, and `.las` files in the input directory.
2. Read points from text or LAS input.
3. Convert points to an occupancy voxel grid.
4. Scan non-overlapping `k x k x k` blocks.
5. Canonicalize each block under the supported rotations.
6. Aggregate pattern frequency, spatial entropy, and 3DSO terms.
7. Write one CSV row per input file.

## Performance Notes

- The common LAS `k=3` path writes directly into packed z-column storage while preserving the default float-coordinate voxelization semantics.
- The common `k=3` block path uses compact 64-bit fingerprints.
- Rotation maps are precomputed once per run.
- Spatial entropy uses dense count buffers in the hot path.
- LAS input has streamed and Windows memory-mapped readers.
- OpenMP is used at the file level when available.

## Source Layout

- `include/tdso/tdso.hpp`: public entry point.
- `include/tdso/results.hpp`: result structures shared by computation and output.
- `src/main.cpp`: executable entry point.
- `src/cli_args.cpp`: command-line parsing and usage text.
- `src/csv_writer.cpp`: CSV header generation, escaping, and row output.
- `src/tdso_engine.cpp`: implementation of input discovery, reading, voxelization, pattern encoding, entropy, and orchestration.

The implementation is intentionally conservative: the current refactor improves project boundaries without changing the numerical algorithm.



