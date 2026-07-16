# Reproducing the WinRT projection throughput benchmark

Step-by-step to reproduce every number in [`README.md`](./README.md) from a clean machine.
Follow it top to bottom. Commands are PowerShell.

## 0. What you are building

One no-op WinRT component (`LangPerf.Class`, defined once in `lang.winmd`) and four consumers
that each call the identical 13-operation sequence against it:

- **Rust** (windows-rs projection)
- **C++/WinRT** (cppwinrt projection)
- **C#/WinRT JIT** (CsWinRT, `dotnet run -c Release`)
- **C#/WinRT NativeAOT** (CsWinRT, `dotnet publish /p:PublishAot=true`)

Plus a **pure-MSVC** C++ build (no cargo, no Rust) that produces the same C++ bench with
`cl.exe` + `link.exe` alone.

## 1. Prerequisites

Install all four. Versions below are what the published run used; newer is fine.

| Tool | Version used | Install |
| :--- | :----------- | :------ |
| Windows | 10/11 **x64** | — |
| Rust (MSVC toolchain) | 1.97.1 `stable-x86_64-pc-windows-msvc` | <https://rustup.rs> then `rustup default stable-x86_64-pc-windows-msvc` |
| .NET SDK | 10.0.301 | <https://dotnet.microsoft.com/download/dotnet/10.0> |
| Visual Studio 2022+ **or** Build Tools | MSVC v143 (14.44), Windows 11 SDK | Installer → **Desktop development with C++** workload |

Notes:

- **`cppwinrt.exe` is embedded** in the windows-rs `cppwinrt` crate — you do **not** install it
  separately.
- NativeAOT pulls its ILC compiler pack automatically on first `dotnet publish`; it shells out
  to the MSVC linker, so the C++ workload above is required for AOT too.
- No debugger is needed to run the benchmark. `cdb.exe` / `dumpbin.exe` are only needed for the
  optional [disassembly](#6-optional-disassembly) section (WinDbg tools + the VS toolchain).

## 2. Clone — to a SHORT path

Clone to a short, local path. **Do not** clone under a deep tree or a OneDrive-synced folder —
WinUI/cppwinrt codegen fails on long paths (`PRI210`, `MSB3073`).

```powershell
git clone https://github.com/jonwis/windows-rs C:\src\windows-rs
cd C:\src\windows-rs
git checkout jonwis/perf-bench
```

Everything below runs from `C:\src\windows-rs`.

## 3. Build + run the full matrix (the headline table)

This builds all four consumers and both components (Release), then runs every
consumer × component combination and prints a matrix.

```powershell
cd C:\src\windows-rs\crates\samples\lang_perf
.\run.ps1 -Iterations 10000000 -IncludeAot
```

- `-Iterations 10000000` is the published count. **The C++ and C#/AOT `Error` loops are slow**
  (the C++/WinRT `Error` loop alone is ~4–7 minutes at 10M because it throws + originates every
  call). Budget ~30–45 minutes for the full `-IncludeAot` run.
- To iterate faster, drop to `-Iterations 1000000` — per-call costs are stable at 1M. The C#
  tiers especially finish far quicker.
- Omit `-IncludeAot` to skip the (slow) NativeAOT publish+run and get just Rust, C++, C#/JIT.

Each combo prints a header line proving which component answered, e.g.
`# Rust consumer -> C++ component - 10000000 iterations`, followed by the 13 timings, then a
final `## Matrix results` table.

### Run one combo in isolation (recommended for clean numbers)

`run.ps1` runs combos sequentially, but to measure a single combo with nothing else building,
run the built exe directly. After one `run.ps1` (or `cargo build --release -p lang_perf_rust -p
lang_perf_cpp -p lang_perf_component -p lang_perf_component_cpp`):

```powershell
cd C:\src\windows-rs
# consumer exe --iterations N --component <rust|cpp>
.\target\release\lang_perf_rust.exe --iterations 10000000 --component rust
.\target\release\lang_perf_rust.exe --iterations 10000000 --component cpp
.\target\release\lang_perf_cpp.exe  --iterations 10000000 --component rust
.\target\release\lang_perf_cpp.exe  --iterations 10000000 --component cpp
```

The consumer stages the chosen component DLL (`langperf_<lang>.dll`) as `LangPerf.dll` next to
itself before running, so activation picks the one you asked for.

### C# JIT / AOT in isolation

The C# consumer resolves `LangPerf.dll` from `PATH`. `run.ps1` stages each component into
`target\release\component_<lang>\` for this. To run one by hand:

```powershell
cd C:\src\windows-rs
$env:PATH = "$PWD\target\release\component_rust;$env:PATH"   # or component_cpp
dotnet run -c Release --project crates\samples\lang_perf\csharp -- --iterations 1000000
```

If NuGet restore fails with a broken local source (e.g. a stale `Foundry Local` /
`d:\...` feed on your machine), disable it just for the restore and re-enable after:

```powershell
dotnet nuget disable source "Foundry Local"
# ... run ...
dotnet nuget enable source "Foundry Local"
```

## 4. Pure-MSVC C++ build (no cargo, no Rust)

Builds `LangPerf.dll` (component) + `lang_perf_cpp_msvc.exe` (bench) using only `cppwinrt.exe`,
`cl.exe`, and `link.exe`, with `-optimize` / `/O2 /Ox /GL` / `/LTCG /OPT:REF /OPT:ICF`.

```powershell
cd C:\src\windows-rs\crates\samples\lang_perf
.\msvc\build.ps1 -Iterations 10000000
```

Prerequisite: it reads `component\lang.winmd`. If you have not built the matrix yet, generate
the winmd first (this is metadata generation, not a Rust binary in the output):

```powershell
cargo build --release -p lang_perf_component --manifest-path C:\src\windows-rs\Cargo.toml
```

Output lands in `crates\samples\lang_perf\msvc\out\`.

## 5. Expected results (published run, 10M iters, ms)

Consumer projecting the **Rust** component. Your absolute numbers will differ by machine; the
*ratios* are the point.

| Path | Rust | C++/WinRT | C#/JIT | C#/AOT |
| :------------- | ----: | --------: | -------: | --------: |
| Create         |   596 |       658 |   12,828 |    21,440 |
| Int32          |    26 |        30 |       65 |       118 |
| String         |    31 |        50 |    1,401 |       290 |
| Object         |   172 |       171 |    1,418 |     1,606 |
| Cast (QI)      |   350 |       348 |    1,681 |     2,853 |
| Event          |   369 |       364 |    1,272 |     1,391 |
| AddRemove      | 1,886 |     1,817 |   34,465 |    68,112 |
| IterateVector  |    15 |       153 |      550 |       495 |
| GetMany        |     5 |         2 |      276 |       235 |
| Map            |   792 |     1,008 |    2,196 |     5,474 |
| Async          |   586 |       578 |   52,766 |   386,319 |
| Reference      |   765 |     2,305 |   30,825 |   115,053 |
| Error          |    67 |   247,220 |   25,948 |    42,509 |

## 6. Optional: disassembly

To reproduce the `Error` throw-path disassembly and the origination-import table.

Requires the WinDbg tools (`cdb.exe`) and the VS toolchain (`dumpbin.exe`).

### 6a. Which binaries link the origination APIs

```powershell
$dumpbin = (Get-ChildItem "${env:ProgramFiles}\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" | Select-Object -First 1).FullName
foreach ($b in @(
    "C:\src\windows-rs\crates\samples\lang_perf\msvc\out\lang_perf_cpp_msvc.exe",
    "C:\src\windows-rs\crates\samples\lang_perf\msvc\out\LangPerf.dll",
    "C:\src\windows-rs\target\release\lang_perf_rust.exe",
    "C:\src\windows-rs\target\release\langperf_rust.dll")) {
    $imp = & $dumpbin /imports $b
    "{0,-28} LanguageException={1} RoOriginateErrorW={2}" -f (Split-Path $b -Leaf),
        @($imp | Select-String 'RoOriginateLanguageException').Count,
        @($imp | Select-String 'RoOriginateError').Count
}
```

Expected: both C++ binaries import `RoOriginateLanguageException`; the Rust component imports
neither.

### 6b. Disassemble the C++ throw path

Build a symbol-enabled component (adds `/Zi`, drops `/GL` so the calls stay visible), then
`uf` the ABI thunk:

```powershell
# from a "x64 Native Tools Command Prompt for VS" (or after calling vcvars64.bat):
cl /nologo /std:c++20 /EHsc /O2 /Zi /I <msvc\out\include> /LD ^
   crates\samples\lang_perf\component_cpp\src\component.cpp ^
   /Fe:LangPerfSym.dll /link /DEBUG:FULL /EXPORT:DllGetActivationFactory onecoreuap.lib

cdb -z LangPerfSym.dll -y . -c "uf winrt::impl::produce<Class,winrt::LangPerf::IClass>::Next; q"
```

You will see the thunk call `Class::Next`, a `catch$0` funclet, and `winrt::to_hresult`, which
reaches `RoOriginateLanguageException`.

## 7. What was changed from stock windows-rs

See [`CHANGES-perf-analysis.md`](https://github.com/jonwis/windows-rs/blob/jonwis/perf-bench/crates/samples/lang_perf/CHANGES-perf-analysis.md)
in the windows-rs fork for the exact diff and rationale: max-opt C++ build flags, the pure-MSVC
build under `msvc/`, and the Rust `ErrorOriginate` variant. The 12 core loops are unchanged stock
harness code.
