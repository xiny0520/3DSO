param(
    [string]$Executable = "",
    [string]$DataDir = "",
    [string]$OutputDir = "",
    [int]$Threads = 1
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
Invoke-ExpectFailure -Name "invalid k voxel below minimum" -CliArgs @(
    "--input-dir", $OutputDir,
    "--output-csv", (Join-Path $OutputDir "invalid.csv"),
    "--k-voxel", "2"
)
Invoke-ExpectFailure -Name "invalid even k voxel" -CliArgs @(
    "--input-dir", $OutputDir,
    "--output-csv", (Join-Path $OutputDir "invalid.csv"),
    "--k-voxel", "4"
)
Invoke-ExpectFailure -Name "invalid k voxel above maximum" -CliArgs @(
    "--input-dir", $OutputDir,
    "--output-csv", (Join-Path $OutputDir "invalid.csv"),
    "--k-voxel", "8"
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

$txtLayersCsv = Join-Path $OutputDir "txt_layers_sample.csv"
Invoke-ExpectSuccess -Name "small txt layers" -CliArgs @(
    "--input-dir", $txtDir,
    "--output-csv", $txtLayersCsv,
    "--k-voxel", "3",
    "--voxel-size", "0.3",
    "--block-ratio", "5",
    "--plot-size", "1", "2", "2",
    "--layers", "2",
    "--threads", "1"
)

$txtLayerRows = @(Import-Csv -LiteralPath $txtLayersCsv)
if ($txtLayerRows.Count -ne 1 -or $txtLayerRows[0].Status -ne "ok" -or
    (([int]$txtLayerRows[0].Npts_L1 + [int]$txtLayerRows[0].Npts_L2) -le 0)) {
    throw "small txt layers produced unexpected output"
}

$dirtyTxtDir = Join-Path $OutputDir "txt_dirty_input"
New-Item -ItemType Directory -Force -Path $dirtyTxtDir | Out-Null
$dirtyTxtPath = Join-Path $dirtyTxtDir "sample.txt"
@"
x y z
0.0 0.0 0.0
0.3 0.0 0.0

not a point
0.0 0.3 0.0
0.0 0.0 0.3
0.3 0.3 0.3
1.2 1.2 1.2
"@ | Set-Content -LiteralPath $dirtyTxtPath -Encoding ASCII

$dirtyTxtCsv = Join-Path $OutputDir "txt_dirty_sample.csv"
Invoke-ExpectSuccess -Name "txt skips blank and bad lines" -CliArgs @(
    "--input-dir", $dirtyTxtDir,
    "--output-csv", $dirtyTxtCsv,
    "--k-voxel", "3",
    "--voxel-size", "0.3",
    "--block-ratio", "5",
    "--plot-size", "1", "2", "2",
    "--threads", "1"
)

$dirtyTxtRows = @(Import-Csv -LiteralPath $dirtyTxtCsv)
if ($dirtyTxtRows.Count -ne 1 -or $dirtyTxtRows[0].Status -ne "ok" -or [int]$dirtyTxtRows[0].NumPoints -ne 6) {
    throw "txt parser did not preserve valid points after blank/bad lines"
}

$boundaryTxtDir = Join-Path $OutputDir "txt_boundary_input"
New-Item -ItemType Directory -Force -Path $boundaryTxtDir | Out-Null
$boundaryTxtPath = Join-Path $boundaryTxtDir "sample.txt"
@"
0.0 0.0 0.0
0.6 0.6 0.6
"@ | Set-Content -LiteralPath $boundaryTxtPath -Encoding ASCII

$boundaryTxtCsv = Join-Path $OutputDir "txt_boundary_sample.csv"
Invoke-ExpectSuccess -Name "txt includes upper voxel boundary" -CliArgs @(
    "--input-dir", $boundaryTxtDir,
    "--output-csv", $boundaryTxtCsv,
    "--k-voxel", "3",
    "--voxel-size", "0.3",
    "--block-ratio", "5",
    "--plot-size", "1", "0.6", "0.6",
    "--threads", "1"
)

$boundaryTxtRows = @(Import-Csv -LiteralPath $boundaryTxtCsv)
if ($boundaryTxtRows.Count -ne 1 -or $boundaryTxtRows[0].Status -ne "ok" -or [int]$boundaryTxtRows[0].TotalBlocks -ne 1) {
    throw "txt parser did not include points on the upper voxel boundary"
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
foreach ($expected in @("3DSO_L1", "3DSO_L2", "Npts_L1", "Npts_L2")) {
    if ($expected -notin $layerHeaders) {
        throw "Layer output is missing column $expected"
    }
}

Write-Host "Regression checks passed."
