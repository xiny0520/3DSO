# Reliability Policy

This project treats numerical stability and reproducibility as higher priority than peak speed.

## Stable Defaults

- All supported input formats use the same stable computation path: read points, build the voxel grid, then compute 3DSO.
- Release binaries should keep `DSO_ENABLE_NATIVE=OFF` unless the goal is a local-only benchmark.
- Paper and public release builds should keep `DSO_ENABLE_FAST_MATH=OFF`. Fast math is a benchmark-only setting because it allows aggressive floating-point transformations.

## Regression Standard

Before accepting a source or build-system change:

1. Build in Release mode.
2. Run `tests/run_regression.ps1`.

Optimizations that change `TotalBlocks`, `NumPatterns`, `Status`, or `NumPoints` are treated as semantic changes, not performance improvements.

