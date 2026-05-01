<#
.SYNOPSIS
    Analyze the thunked runtime class sizes and call-site code in cppwinrt-proj.exe.
.DESCRIPTION
    1. Runs dumpbin /disasm filtered to thunk-related symbols.
    2. Reports sizeof(InterfaceThunk), sizeof(ThunkedRuntimeClass<3>), sizeof(PropertySet).
    3. Counts instruction bytes at key call sites (Insert, Size, First, etc.).
    4. Reports total .text section size.
#>
[CmdletBinding()]
param(
    [string]$ExePath = "F:\holding\jonwis.github.io\out\build\vsbuild\code\cppwinrt-proj\RelWithDebInfo\cppwinrt-proj.exe"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $PSScriptRoot
$disasmScript = Join-Path $scriptRoot "scripts\disassemble-exe.ps1"

# Symbols to extract for analysis
$thunkSymbols = @(
    "create_and_view_thunked"
    "create_and_view_cppwinrt"
    "thunk_test"
    "generic_mutating"
    "InterfaceThunk"
    "ThunkedRuntimeClass"
    "PropertySet"
    "resolve_thunk"
    "comparison"
)

$outDir = Split-Path -Parent $ExePath
$disasmFile = Join-Path $outDir "thunk_analysis.disasm.asm"

Write-Host "=== Thunk Runtime Class Size Analysis ===" -ForegroundColor Cyan
Write-Host ""

# Step 1: Check exe exists
if (-not (Test-Path $ExePath)) {
    Write-Error "Executable not found: $ExePath"
    return
}

$fileInfo = Get-Item $ExePath
Write-Host "Binary: $($fileInfo.FullName)"
Write-Host "Binary size: $([math]::Round($fileInfo.Length / 1024, 1)) KB"
Write-Host ""

# Step 2: Use dumpbin /headers to get .text section size
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$installPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
$dumpbin = Get-ChildItem -Path (Join-Path $installPath "VC\Tools\MSVC") -Filter "dumpbin.exe" -Recurse |
    Where-Object { $_.DirectoryName -match 'x64' } |
    Select-Object -First 1 -ExpandProperty FullName

if ($dumpbin) {
    Write-Host "--- Section Sizes ---" -ForegroundColor Yellow
    $headers = & $dumpbin /nologo /headers $ExePath 2>&1
    $textSection = $false
    foreach ($line in $headers) {
        if ($line -match 'SECTION HEADER.*\.text') { $textSection = $true; continue }
        if ($textSection -and $line -match '^\s+([0-9A-F]+) virtual size') {
            $textSize = [Convert]::ToInt64($Matches[1], 16)
            Write-Host ".text section: $textSize bytes ($([math]::Round($textSize / 1024, 1)) KB)"
            $textSection = $false
        }
        if ($textSection -and $line -match 'SECTION HEADER') { $textSection = $false }
    }
    Write-Host ""
}

# Step 3: Produce filtered disassembly
Write-Host "--- Generating Filtered Disassembly ---" -ForegroundColor Yellow
& $disasmScript -ExePath $ExePath -OutPath $disasmFile -Symbols $thunkSymbols -Tool dumpbin
Write-Host ""

# Step 4: Parse disassembly and measure function sizes
Write-Host "--- Thunk-Related Function Sizes ---" -ForegroundColor Yellow

$lines = Get-Content $disasmFile
$functions = [ordered]@{}
$currentFunc = $null
$currentStart = $null
$currentLines = 0

# dumpbin heading pattern: decorated or undecorated symbol name followed by colon
$headingRe = '^\s*(?<name>[^\s:][^:]+):\s*$'
$instrRe = '^\s+[0-9A-Fa-f]+:'

for ($i = 0; $i -lt $lines.Count; $i++) {
    $line = $lines[$i]
    $hm = [regex]::Match($line, $headingRe)
    if ($hm.Success) {
        if ($currentFunc -and $currentLines -gt 0) {
            $functions[$currentFunc] = $currentLines
        }
        $currentFunc = $hm.Groups['name'].Value.Trim()
        $currentLines = 0
        continue
    }
    if ($line -match $instrRe) {
        $currentLines++
    }
}
if ($currentFunc -and $currentLines -gt 0) {
    $functions[$currentFunc] = $currentLines
}

# Categorize and display
$categories = [ordered]@{
    "Thunk Infrastructure" = @("resolve", "init_thunk", "InterfaceThunk", "resolve_thunk")
    "ThunkedRuntimeClass"  = @("ThunkedRuntimeClass", "attach", "clear")
    "PropertySet (thunked)"= @("generic_mutating.*PropertySet", "create_and_view_thunked")
    "PropertySet (cppwinrt)"= @("create_and_view_cppwinrt")
    "Comparison"           = @("comparison")
}

$totalInstr = 0
foreach ($cat in $categories.Keys) {
    $patterns = $categories[$cat]
    $matched = $functions.Keys | Where-Object {
        $name = $_
        $patterns | Where-Object { $name -match $_ } | Select-Object -First 1
    }
    if ($matched) {
        Write-Host "  [$cat]" -ForegroundColor Green
        foreach ($fn in $matched) {
            $count = $functions[$fn]
            $totalInstr += $count
            $truncName = if ($fn.Length -gt 90) { $fn.Substring(0, 87) + "..." } else { $fn }
            Write-Host ("    {0,-90} {1,5} instr" -f $truncName, $count)
        }
    }
}

Write-Host ""
Write-Host "Total thunk-related instructions: $totalInstr" -ForegroundColor Cyan

# Step 5: Compile-time sizeof estimates from the structure definitions
Write-Host ""
Write-Host "--- Compile-Time Size Estimates (x64) ---" -ForegroundColor Yellow
Write-Host "  sizeof(InterfaceThunk)         = 32 bytes (4 pointers: vtable, default_abi, cache_slot, iid)"
Write-Host "  sizeof(ThunkedRuntimeClass<3>) = 8 (iids_) + 4*8 (cache) + 3*32 (thunks) = 136 bytes"
Write-Host "  sizeof(PropertySet)            = 136 bytes (no additional fields)"
Write-Host "  sizeof(std::atomic<void*>)     = 8 bytes"
Write-Host ""
Write-Host "  For comparison:"
Write-Host "  sizeof(winrt::...::PropertySet) = 8 bytes (single interface pointer)"
Write-Host ""

# Step 6: Show disasm file location
Write-Host "Full disassembly: $disasmFile" -ForegroundColor Gray
