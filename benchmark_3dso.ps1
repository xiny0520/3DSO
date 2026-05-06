param(
    [string]$Executable = "",
    [string]$InputDir = "",
    [string]$OutputDir = "",
    [int]$Threads = 8,
    [int]$Repeats = 3,
    [switch]$SkipWarmup,
    [int]$KVoxel = 3,
    [double]$VoxelSize = 0.1,
    [int]$BlockRatio = 5,
    [int]$PlotSizeMode = 1,
    [double]$PlotSizeX = 25.0,
    [double]$PlotSizeY = 25.0,
    [int]$Layers = 0
)

$ErrorActionPreference = "Stop"
$ScriptRoot = Split-Path -Parent $PSCommandPath

if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path $ScriptRoot "build_release_vs\Release\3dso.exe"
}
if ([string]::IsNullOrWhiteSpace($InputDir)) {
    $InputDir = Join-Path $ScriptRoot "data"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $ScriptRoot "benchmark_runs"
}

if (-not (Test-Path -LiteralPath $Executable)) {
    throw "Executable not found: $Executable"
}

if (-not (Test-Path -LiteralPath $InputDir)) {
    throw "Input directory not found: $InputDir"
}

$inputFiles = Get-ChildItem -LiteralPath $InputDir -File | Where-Object {
    $_.Extension -in ".las", ".xyz", ".txt" -and $_.Name -ne "BENCHMARK_DATASET.txt"
} | Sort-Object Name
if ($inputFiles.Count -eq 0) {
    throw "No benchmark files found in: $InputDir"
}

if ($Repeats -lt 1) {
    throw "Repeats must be >= 1"
}

if (-not (Test-Path -LiteralPath $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$sessionDir = Join-Path $OutputDir ("benchmark_" + $timestamp)
New-Item -ItemType Directory -Path $sessionDir | Out-Null

function Invoke-3dsoRun {
    param(
        [string]$RunLabel,
        [string]$CsvPath
    )

    $args = @(
        "--input-dir", $InputDir,
        "--output-csv", $CsvPath,
        "--k-voxel", $KVoxel,
        "--voxel-size", $VoxelSize,
        "--block-ratio", $BlockRatio,
        "--plot-size", $PlotSizeMode, $PlotSizeX, $PlotSizeY,
        "--threads", $Threads,
        "--layers", $Layers
    )

    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    & $Executable @args
    $exitCode = $LASTEXITCODE
    $stopwatch.Stop()

    if ($exitCode -ne 0) {
        throw "Run '$RunLabel' failed with exit code $exitCode"
    }

    [PSCustomObject]@{
        run_label       = $RunLabel
        elapsed_seconds = [Math]::Round($stopwatch.Elapsed.TotalSeconds, 6)
        output_csv      = $CsvPath
    }
}

Write-Host "Benchmark executable: $Executable"
Write-Host "Benchmark input dir : $InputDir"
Write-Host "Benchmark file count: $($inputFiles.Count)"
Write-Host "Benchmark output dir: $sessionDir"
Write-Host "Threads             : $Threads"
Write-Host "Repeats             : $Repeats"

$results = @()

if (-not $SkipWarmup) {
    $warmupCsv = Join-Path $sessionDir "warmup_results.csv"
    Write-Host ""
    Write-Host "Running warmup..."
    $warmupResult = Invoke-3dsoRun -RunLabel "warmup" -CsvPath $warmupCsv
    Write-Host ("Warmup finished in {0} s" -f $warmupResult.elapsed_seconds)
}

for ($i = 1; $i -le $Repeats; $i++) {
    $runName = "run_{0:d2}" -f $i
    $csvPath = Join-Path $sessionDir ($runName + "_results.csv")
    Write-Host ""
    Write-Host ("Running {0}..." -f $runName)
    $result = Invoke-3dsoRun -RunLabel $runName -CsvPath $csvPath
    $results += $result
    Write-Host ("{0} finished in {1} s" -f $runName, $result.elapsed_seconds)
}

$summaryPath = Join-Path $sessionDir "benchmark_summary.csv"
$results | Export-Csv -Path $summaryPath -NoTypeInformation -Encoding UTF8

$elapsedValues = $results | ForEach-Object { $_.elapsed_seconds }
$avg = ($elapsedValues | Measure-Object -Average).Average
$min = ($elapsedValues | Measure-Object -Minimum).Minimum
$max = ($elapsedValues | Measure-Object -Maximum).Maximum

Write-Host ""
Write-Host "Benchmark summary"
Write-Host ("Average: {0:N6} s" -f $avg)
Write-Host ("Min    : {0:N6} s" -f $min)
Write-Host ("Max    : {0:N6} s" -f $max)
Write-Host ("CSV log: {0}" -f $summaryPath)
