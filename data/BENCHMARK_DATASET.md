# Benchmark Dataset

This directory is used by `benchmark_3dso.ps1` as the default benchmark input.

The full benchmark set contains 100 sorted LAS plot files. The LAS files are intentionally ignored by `.gitignore` because they are large binary data and should usually be distributed separately from the source repository.

Suggested local layout:

```text
data/
  BENCHMARK_DATASET.md
  Plot_C10_R10.las
  Plot_C10_R11.las
  ...
```

Suggested benchmark command:

```powershell
.\benchmark_3dso.ps1 -Threads 8 -Repeats 3
```

For reproducible published timings, record the dataset version or checksum, CPU model, compiler version, CMake options, and thread count.


