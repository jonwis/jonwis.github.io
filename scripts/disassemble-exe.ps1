[CmdletBinding()]
param(
    [string]$ExePath = "F:\holding\jonwis.github.io\out\build\vsbuild\code\cppwinrt-proj\RelWithDebInfo\cppwinrt-proj.exe",
    [string]$OutPath,
    [ValidateSet("auto", "llvm", "dumpbin")]
    [string]$Tool = "auto",
    [string[]]$Symbols,
    [switch]$IncludeSource,
    [switch]$VerboseToolOutput
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-ExistingPath {
    param([Parameter(Mandatory = $true)][string]$PathValue)

    try {
        return (Resolve-Path -Path $PathValue).Path
    }
    catch {
        throw "Path not found: $PathValue"
    }
}

function Find-VsTool {
    param([Parameter(Mandatory = $true)][string]$ToolName)

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -Path $vswhere)) {
        return $null
    }

    $installPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if (-not $installPath) {
        return $null
    }

    $candidates = @(
        (Join-Path $installPath "VC\Tools\MSVC")
        (Join-Path $installPath "VC\Tools\Llvm\x64\bin")
    )

    foreach ($candidate in $candidates) {
        if (-not (Test-Path -Path $candidate)) {
            continue
        }

        $found = Get-ChildItem -Path $candidate -Filter $ToolName -Recurse -File -ErrorAction SilentlyContinue |
            Sort-Object -Property FullName -Descending |
            Select-Object -First 1

        if ($found) {
            return $found.FullName
        }
    }

    return $null
}

function Find-ToolPath {
    param([Parameter(Mandatory = $true)][string]$ToolName)

    $cmd = Get-Command -Name $ToolName -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    return Find-VsTool -ToolName $ToolName
}

function Invoke-ToolToFile {
    param(
        [Parameter(Mandatory = $true)][string]$ToolPath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$OutputFile
    )

    if ($VerboseToolOutput) {
        Write-Host "Running: $ToolPath $($Arguments -join ' ')"
    }

    $allOutput = & $ToolPath @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        $joined = ($allOutput | Out-String)
        throw "Tool failed ($ToolPath) with exit code $LASTEXITCODE.`n$joined"
    }

    $allOutput | Set-Content -Path $OutputFile -Encoding ascii
}

function ConvertTo-UndecoratedDisassembly {
    param(
        [Parameter(Mandatory = $true)][string]$InputPath,
        [Parameter(Mandatory = $true)][string]$OutputPath,
        [Parameter(Mandatory = $true)][string]$UndnamePath
    )

    $lines = Get-Content -Path $InputPath
    $pattern = "\?[A-Za-z_][A-Za-z0-9_@$?]*"

    $symbols = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    foreach ($line in $lines) {
        foreach ($m in [System.Text.RegularExpressions.Regex]::Matches($line, $pattern)) {
            [void]$symbols.Add($m.Value)
        }
    }

    $map = @{}
    foreach ($sym in $symbols) {
        $decoded = & $UndnamePath $sym 2>$null
        if ($LASTEXITCODE -eq 0 -and $decoded) {
            $demangled = $decoded |
                ForEach-Object { $_.ToString().Trim() } |
                Where-Object { $_ -and $_ -notmatch '^Microsoft \(R\) C\+\+ Name Undecorator' } |
                Select-Object -Last 1
            if ($demangled -and $demangled -ne $sym) {
                $map[$sym] = $demangled
            }
        }
    }

    if ($map.Count -eq 0) {
        Copy-Item -Path $InputPath -Destination $OutputPath -Force
        return
    }

    $headingPattern = '^\s*(?<sym>\?[A-Za-z_][A-Za-z0-9_@$?]*):\s*$'
    $outLines = foreach ($line in $lines) {
        $m = [System.Text.RegularExpressions.Regex]::Match($line, $headingPattern)
        if ($m.Success) {
            $sym = $m.Groups['sym'].Value
            if ($map.ContainsKey($sym)) {
                $line.Replace($sym, $map[$sym])
                continue
            }
        }
        $line
    }

    $outLines | Set-Content -Path $OutputPath -Encoding ascii
}

function Normalize-SymbolName {
    param([Parameter(Mandatory = $true)][string]$Name)

    $n = $Name.Trim().ToLowerInvariant()
    $bang = $n.IndexOf('!')
    if ($bang -ge 0 -and $bang -lt ($n.Length - 1)) {
        $n = $n.Substring($bang + 1)
    }
    return $n
}

function Get-HeadingSymbolName {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Line)

    if ([string]::IsNullOrWhiteSpace($Line)) {
        return $null
    }

    $m1 = [System.Text.RegularExpressions.Regex]::Match($Line, '^\s*[0-9a-f`]+\s+<(?<name>.+)>:\s*$', [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if ($m1.Success) {
        return $m1.Groups['name'].Value.Trim()
    }

    $m2 = [System.Text.RegularExpressions.Regex]::Match($Line, '^\s*[0-9a-f`]+\s+(?<name>[^:]+):\s*$', [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if ($m2.Success) {
        return $m2.Groups['name'].Value.Trim()
    }

    $m3 = [System.Text.RegularExpressions.Regex]::Match($Line, '^\s*(?<name>[^:]+):\s*$')
    if ($m3.Success) {
        $name = $m3.Groups['name'].Value.Trim()
        if ($name -notmatch '^[0-9a-f`]+$' -and $name -notmatch '^link\s*$' -and $name -notmatch '^file\s+type\s*$') {
            return $name
        }
    }

    return $null
}

function Filter-DisassemblyBySymbols {
    param(
        [Parameter(Mandatory = $true)][string]$InputPath,
        [Parameter(Mandatory = $true)][string]$OutputPath,
        [Parameter(Mandatory = $true)][string[]]$RequestedSymbols
    )

    $expandedRequestedSymbols = @(
        foreach ($sym in $RequestedSymbols) {
            if ([string]::IsNullOrWhiteSpace($sym)) {
                continue
            }

            foreach ($part in ($sym -split ',')) {
                if (-not [string]::IsNullOrWhiteSpace($part)) {
                    $part.Trim()
                }
            }
        }
    )

    $requests = @(
        $expandedRequestedSymbols |
            ForEach-Object {
                $raw = Normalize-SymbolName -Name $_
                $tokens = @(
                    ($raw -replace '[^a-z0-9]+', ' ').Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries) |
                        Where-Object { $_.Length -ge 4 }
                )
                [pscustomobject]@{
                    Raw = $raw
                    Tokens = $tokens
                }
            }
    )

    if ($requests.Count -eq 0) {
        Copy-Item -Path $InputPath -Destination $OutputPath -Force
        return
    }

    $lines = @(Get-Content -Path $InputPath)
    if ($lines.Count -eq 0) {
        Copy-Item -Path $InputPath -Destination $OutputPath -Force
        return
    }

    $headings = @()
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $name = Get-HeadingSymbolName -Line $lines[$i]
        if ($null -ne $name) {
            $headings += [pscustomobject]@{
                Index = $i
                Name = $name
                NormalizedName = (Normalize-SymbolName -Name $name)
            }
        }
    }

    if ($headings.Count -eq 0) {
        Write-Warning "No function heading pattern found in disassembly; writing unfiltered output."
        Copy-Item -Path $InputPath -Destination $OutputPath -Force
        return
    }

    $selected = [System.Collections.Generic.List[string]]::new()
    $matchedHeadings = [System.Collections.Generic.List[string]]::new()

    for ($h = 0; $h -lt $headings.Count; $h++) {
        $start = $headings[$h].Index
        $end = if ($h + 1 -lt $headings.Count) { $headings[$h + 1].Index - 1 } else { $lines.Count - 1 }

        $haystack = ($headings[$h].NormalizedName -replace '[^a-z0-9]+', ' ')
        $isMatch = $false
        foreach ($request in $requests) {
            if ($headings[$h].NormalizedName.Contains($request.Raw)) {
                $isMatch = $true
                break
            }

            if ($request.Tokens.Count -gt 0) {
                $allTokensMatch = $true
                foreach ($t in $request.Tokens) {
                    if ($haystack -notlike "*${t}*") {
                        $allTokensMatch = $false
                        break
                    }
                }
                if ($allTokensMatch) {
                    $isMatch = $true
                    break
                }
            }
        }

        if ($isMatch) {
            $matchedHeadings.Add($headings[$h].Name) | Out-Null
            for ($j = $start; $j -le $end; $j++) {
                $selected.Add($lines[$j]) | Out-Null
            }
            $selected.Add('') | Out-Null
        }
    }

    if ($selected.Count -eq 0) {
        Write-Warning "No requested symbols matched disassembly headings; writing unfiltered output."
        Copy-Item -Path $InputPath -Destination $OutputPath -Force
        return
    }

    $header = [System.Collections.Generic.List[string]]::new()
    $header.Add('; Filtered disassembly output') | Out-Null
    $header.Add('; Requested symbols:') | Out-Null
    foreach ($rs in $expandedRequestedSymbols) {
        $header.Add(';   ' + $rs) | Out-Null
    }
    $header.Add('; Matched headings:') | Out-Null
    foreach ($mh in ($matchedHeadings | Sort-Object -Unique)) {
        $header.Add(';   ' + $mh) | Out-Null
    }
    $header.Add('') | Out-Null

    ($header + $selected) | Set-Content -Path $OutputPath -Encoding ascii
}

$resolvedExe = Resolve-ExistingPath -PathValue $ExePath
$pdbPath = [System.IO.Path]::ChangeExtension($resolvedExe, ".pdb")
$hasPdb = Test-Path -Path $pdbPath

if ([string]::IsNullOrWhiteSpace($OutPath)) {
    $outDir = Split-Path -Path $resolvedExe -Parent
    $base = [System.IO.Path]::GetFileNameWithoutExtension($resolvedExe)
    $OutPath = Join-Path $outDir ($base + ".disasm.asm")
}

$outDirFinal = Split-Path -Path $OutPath -Parent
if (-not [string]::IsNullOrWhiteSpace($outDirFinal) -and -not (Test-Path -Path $outDirFinal)) {
    New-Item -Path $outDirFinal -ItemType Directory -Force | Out-Null
}

$llvmObjdump = Find-ToolPath -ToolName "llvm-objdump.exe"
$dumpbin = Find-ToolPath -ToolName "dumpbin.exe"
$undname = Find-ToolPath -ToolName "undname.exe"

$useLllvm = $false
switch ($Tool) {
    "llvm" { $useLllvm = $true }
    "dumpbin" { $useLllvm = $false }
    default { $useLllvm = [bool]$llvmObjdump }
}

$tempRaw = [System.IO.Path]::GetTempFileName()
$tempProcessed = [System.IO.Path]::GetTempFileName()
$tempPost = [System.IO.Path]::GetTempFileName()
$usedDumpbin = $false
try {
    if ($useLllvm) {
        if (-not $llvmObjdump) {
            throw "llvm-objdump.exe was requested but not found on PATH or Visual Studio install."
        }

        $toolArgs = @(
            "--disassemble",
            "--line-numbers",
            "--demangle",
            "--x86-asm-syntax=intel",
            $resolvedExe
        )

        if ($IncludeSource) {
            $toolArgs = @("--source") + $toolArgs
        }

        Invoke-ToolToFile -ToolPath $llvmObjdump -Arguments $toolArgs -OutputFile $tempRaw

        # llvm already demangles.
        Copy-Item -Path $tempRaw -Destination $tempProcessed -Force
    }
    else {
        $usedDumpbin = $true
        if (-not $dumpbin) {
            throw "dumpbin.exe not found on PATH or Visual Studio install."
        }

        $toolArgs = @(
            "/nologo",
            "/disasm:nobytes",
            "/linenumbers",
            "/raWData:none",
            $resolvedExe
        )

        Invoke-ToolToFile -ToolPath $dumpbin -Arguments $toolArgs -OutputFile $tempRaw

        # Keep raw decorated names for matching; undecoration is applied after filtering.
        Copy-Item -Path $tempRaw -Destination $tempProcessed -Force
    }

    if ($Symbols -and $Symbols.Count -gt 0) {
        Filter-DisassemblyBySymbols -InputPath $tempProcessed -OutputPath $OutPath -RequestedSymbols $Symbols
    }
    else {
        Copy-Item -Path $tempProcessed -Destination $OutPath -Force
    }

    if ($usedDumpbin) {
        if ($undname) {
            ConvertTo-UndecoratedDisassembly -InputPath $OutPath -OutputPath $tempPost -UndnamePath $undname
            Copy-Item -Path $tempPost -Destination $OutPath -Force
        }
        else {
            Write-Warning "undname.exe not found; output may contain decorated symbols."
        }
    }
}
finally {
    if (Test-Path -Path $tempRaw) {
        Remove-Item -Path $tempRaw -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path -Path $tempProcessed) {
        Remove-Item -Path $tempProcessed -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path -Path $tempPost) {
        Remove-Item -Path $tempPost -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "Wrote disassembly: $OutPath"
if ($hasPdb) {
    Write-Host "PDB detected: $pdbPath"
}
else {
    Write-Warning "No sibling PDB found at $pdbPath. Symbol and line fidelity may be reduced."
}
