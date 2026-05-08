# Tests

This directory contains lightweight regression checks for the 3DSO command-line tool.

The tests prioritize safety and reproducibility over benchmark speed:

- CLI help exits successfully.
- Invalid numeric arguments fail.
- Empty input directories are handled without crashing.
- A small text point-cloud file produces a valid CSV row.

Run from the project root:

```powershell
.\tests\run_regression.ps1 `
  -Executable .\build_release_vs\Release\3dso.exe `
  -DataDir .\data `
  -Threads 1
```

