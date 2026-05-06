# Reliability Policy

This project treats numerical stability and reproducibility as higher priority than peak speed.

## Stable Defaults

- The default LAS path may use direct packed voxel storage for `k_voxel = 3`, but it must preserve the conservative float-coordinate voxelization semantics.
- The conservative LAS path remains available through `--disable-direct-packed-las`.
- Release binaries should keep `DSO_ENABLE_NATIVE=OFF` unless the goal is a local-only benchmark.
- Paper and public release builds should keep `DSO_ENABLE_FAST_MATH=OFF`. Fast math is a benchmark-only setting because it allows aggressive floating-point transformations.

## Experimental Options

- `--experimental-direct-las` is not part of the stable default path.
- `--experimental-packed-columns` is only for explicitly forcing packed storage on non-default paths.
- New optimizations must keep a conservative fallback path until output parity is proven.

## Regression Standard

Before accepting an optimization:

1. Build in Release mode.
2. Run `tests/run_regression.ps1`.
3. Run `scripts/compare_fast_path.ps1` with representative LAS data.
4. Compare CSV output with exact matches for integer/status fields and tight tolerance for floating-point fields.

Optimizations that change `TotalBlocks`, `NumPatterns`, `Status`, or `NumPoints` are treated as semantic changes, not performance improvements.

