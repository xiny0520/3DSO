# Tests

This directory contains lightweight regression checks for the 3DSO command-line tool.

The tests prioritize safety and reproducibility over benchmark speed:

- CLI help exits successfully.
- Invalid numeric arguments fail.
- Empty input directories are handled without crashing.
- A small text point-cloud file produces a valid CSV row.
- When LAS benchmark data is available, the default packed LAS fast path is compared against the conservative path using `--disable-direct-packed-las`.

Run from the project root:

```powershell
.\tests\run_regression.ps1 `
  -Executable .\build_release_vs\Release\3dso.exe `
  -DataDir .\data `
  -LasLimit 10 `
  -Threads 1
```

For release validation, set `-LasLimit 0` to compare all LAS files in `data/`.

