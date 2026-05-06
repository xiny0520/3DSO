param(
    [string]$Executable = "",
    [string]$InputDir = "",
    [string]$OutputDir = "",
    [int]$Limit = 10,
    [int]$Threads = 1,
    [double]$Tolerance = 1e-9
)

$ErrorActionPreference = "Stop"
$ScriptRoot = Split-Path -Parent $PSCommandPath
$ProjectRoot = Split-Path -Parent $ScriptRoot

if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path $ProjectRoot "build_release_vs\Release\3dso.exe"
}
if ([string]::IsNullOrWhiteSpace($InputDir)) {
    $InputDir = Join-Path $ProjectRoot "data"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $ProjectRoot "build_fast_path_check"
}

if (-not (Test-Path -LiteralPath $Executable)) {
    throw "Executable not found: $Executable"
}
if (-not (Test-Path -LiteralPath $InputDir)) {
    throw "Input directory not found: $InputDir"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$baselineCsv = Join-Path $OutputDir "baseline.csv"
$fastCsv = Join-Path $OutputDir "packed_columns.csv"

$commonArgs = @(
    "--input-dir", $InputDir,
    "--k-voxel", "3",
    "--voxel-size", "0.3",
    "--block-ratio", "5",
    "--plot-size", "1", "25", "25",
    "--threads", $Threads,
    "--limit", $Limit
)

& $Executable @commonArgs "--output-csv" $baselineCsv "--disable-direct-packed-las"
if ($LASTEXITCODE -ne 0) {
    throw "Baseline run failed with exit code $LASTEXITCODE"
}

& $Executable @commonArgs "--output-csv" $fastCsv
if ($LASTEXITCODE -ne 0) {
    throw "Packed-columns run failed with exit code $LASTEXITCODE"
}

$baseline = Import-Csv -LiteralPath $baselineCsv
$fast = Import-Csv -LiteralPath $fastCsv

if ($baseline.Count -ne $fast.Count) {
    throw "Row count differs: baseline=$($baseline.Count), fast=$($fast.Count)"
}

$exactColumns = @("PlotID", "SourceFile", "NumPatterns", "TotalBlocks", "Nx", "Ny", "Nz", "NumPoints", "Status")
$numericColumns = @(
    "3DSO", "3DSO_raw", "Iw_total", "Ib_total", "Ic_total",
    "H_sp_Global", "H_sp_Global_norm", "HR98", "BaseArea"
)

$differences = @()
for ($i = 0; $i -lt $baseline.Count; $i++) {
    foreach ($column in $exactColumns) {
        if ($baseline[$i].$column -ne $fast[$i].$column) {
            $differences += "row=$i column=$column baseline=$($baseline[$i].$column) fast=$($fast[$i].$column)"
        }
    }
    foreach ($column in $numericColumns) {
        $delta = [Math]::Abs([double]$baseline[$i].$column - [double]$fast[$i].$column)
        if ($delta -gt $Tolerance) {
            $differences += "row=$i column=$column delta=$delta baseline=$($baseline[$i].$column) fast=$($fast[$i].$column)"
        }
    }
}

if ($differences.Count -gt 0) {
    $preview = $differences | Select-Object -First 20
    throw "Fast path differs from baseline:`n$($preview -join "`n")"
}

Write-Host "Fast path matches baseline for $($baseline.Count) rows. Tolerance=$Tolerance"
