param(
    [string]$Executable = "",
    [string]$DataDir = "",
    [string]$OutputDir = "",
    [int]$LasLimit = 10,
    [int]$Threads = 1,
    [double]$Tolerance = 1e-9
)

$ErrorActionPreference = "Stop"
$ScriptRoot = Split-Path -Parent $PSCommandPath
$ProjectRoot = Split-Path -Parent $ScriptRoot

if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path $ProjectRoot "build_release_vs\Release\3dso.exe"
}
if ([string]::IsNullOrWhiteSpace($DataDir)) {
    $DataDir = Join-Path $ProjectRoot "data"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $ProjectRoot "build_regression"
}

if (-not (Test-Path -LiteralPath $Executable)) {
    throw "Executable not found: $Executable"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

function Invoke-ExpectSuccess {
    param(
        [string]$Name,
        [string[]]$CliArgs
    )

    Write-Host "[RUN ] $Name"
    & $Executable @CliArgs
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
    Write-Host "[ OK ] $Name"
}

function Invoke-ExpectFailure {
    param(
        [string]$Name,
        [string[]]$CliArgs
    )

    Write-Host "[RUN ] $Name"
    & $Executable @CliArgs
    if ($LASTEXITCODE -eq 0) {
        throw "$Name unexpectedly succeeded"
    }
    Write-Host "[ OK ] $Name"
}

Invoke-ExpectSuccess -Name "help" -CliArgs @("--help")
Invoke-ExpectFailure -Name "missing argument value" -CliArgs @(
    "--input-dir"
)
Invoke-ExpectFailure -Name "unknown option" -CliArgs @(
    "--not-a-real-option"
)
Invoke-ExpectFailure -Name "invalid voxel size" -CliArgs @(
    "--input-dir", $OutputDir,
    "--output-csv", (Join-Path $OutputDir "invalid.csv"),
    "--voxel-size", "0"
)
Invoke-ExpectFailure -Name "invalid block ratio" -CliArgs @(
    "--input-dir", $OutputDir,
    "--output-csv", (Join-Path $OutputDir "invalid.csv"),
    "--block-ratio", "0"
)

$emptyDir = Join-Path $OutputDir "empty_input"
New-Item -ItemType Directory -Force -Path $emptyDir | Out-Null
Invoke-ExpectSuccess -Name "empty input directory" -CliArgs @(
    "--input-dir", $emptyDir,
    "--output-csv", (Join-Path $OutputDir "empty.csv")
)

$txtDir = Join-Path $OutputDir "txt_input"
New-Item -ItemType Directory -Force -Path $txtDir | Out-Null
$txtPath = Join-Path $txtDir "sample.txt"
@"
0.0 0.0 0.0
0.3 0.0 0.0
0.0 0.3 0.0
0.0 0.0 0.3
0.3 0.3 0.3
1.2 1.2 1.2
"@ | Set-Content -LiteralPath $txtPath -Encoding ASCII

$txtCsv = Join-Path $OutputDir "txt_sample.csv"
Invoke-ExpectSuccess -Name "small txt input" -CliArgs @(
    "--input-dir", $txtDir,
    "--output-csv", $txtCsv,
    "--k-voxel", "3",
    "--voxel-size", "0.3",
    "--block-ratio", "5",
    "--plot-size", "1", "2", "2",
    "--threads", "1"
)

$txtRows = @(Import-Csv -LiteralPath $txtCsv)
if ($txtRows.Count -ne 1 -or $txtRows[0].Status -ne "ok") {
    throw "small txt input produced unexpected output"
}

$csvNameDir = Join-Path $OutputDir "csv_name_input"
New-Item -ItemType Directory -Force -Path $csvNameDir | Out-Null
Copy-Item -LiteralPath $txtPath -Destination (Join-Path $csvNameDir 'sample,comma.txt') -Force
$csvNameOut = Join-Path $OutputDir "csv_escaped_name.csv"
Invoke-ExpectSuccess -Name "csv escaping for source names" -CliArgs @(
    "--input-dir", $csvNameDir,
    "--output-csv", $csvNameOut,
    "--k-voxel", "3",
    "--voxel-size", "0.3",
    "--block-ratio", "5",
    "--plot-size", "1", "2", "2",
    "--threads", "1"
)
$csvNameRows = @(Import-Csv -LiteralPath $csvNameOut)
if ($csvNameRows.Count -ne 1 -or $csvNameRows[0].SourceFile -ne 'sample,comma.txt') {
    throw "CSV escaping did not preserve source filename"
}

$layerBadDir = Join-Path $OutputDir "layer_bad_input"
New-Item -ItemType Directory -Force -Path $layerBadDir | Out-Null
Set-Content -LiteralPath (Join-Path $layerBadDir "empty.txt") -Value "" -Encoding ASCII
$layerEmptyOut = Join-Path $OutputDir "layers_empty.csv"
Invoke-ExpectSuccess -Name "layers empty input row shape" -CliArgs @(
    "--input-dir", $layerBadDir,
    "--output-csv", $layerEmptyOut,
    "--layers", "2"
)
$layerHeaders = @((Import-Csv -LiteralPath $layerEmptyOut | Get-Member -MemberType NoteProperty | Select-Object -ExpandProperty Name))
foreach ($expected in @("3DSO_L1", "3DSO_L2", "Ic_L1", "Ic_L2", "Hsp_L1", "Hsp_L2", "Npts_L1", "Npts_L2")) {
    if ($expected -notin $layerHeaders) {
        throw "Layer output is missing column $expected"
    }
}

$lasFiles = @()
if (Test-Path -LiteralPath $DataDir) {
    $lasFiles = @(Get-ChildItem -LiteralPath $DataDir -File -Filter "*.las" | Select-Object -First 1)
}

if ($lasFiles.Count -gt 0) {
    $compareScript = Join-Path $ProjectRoot "scripts\compare_fast_path.ps1"
    & $compareScript `
        -Executable $Executable `
        -InputDir $DataDir `
        -OutputDir (Join-Path $OutputDir "fast_path_compare") `
        -Limit $LasLimit `
        -Threads $Threads `
        -Tolerance $Tolerance
    if ($LASTEXITCODE -ne 0) {
        throw "fast path comparison failed with exit code $LASTEXITCODE"
    }
} else {
    Write-Host "[SKIP] LAS fast path comparison: no .las files found in $DataDir"
}

Write-Host "Regression checks passed."
