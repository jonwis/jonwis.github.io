# Comparing Rust, C++/WinRT, and C#/WinRT projection throughput

Per-call cost of the four supported WinRT language projections — windows-rs (Rust), C++/WinRT,
and C#/WinRT in both JIT and NativeAOT — measured along thirteen common interop paths, with the
source and disassembly behind each gap and the change each projection needs to reach parity.
Full reproduction steps are in [`REPRODUCE.md`](./REPRODUCE.md).

## Problem statement

The four supported WinRT language projections — windows-rs (Rust), C++/WinRT, and C#/WinRT
in both JIT and NativeAOT — do not have equal per-call cost on common interop. On the
identical no-op component and the identical call sequence, the spread across "common"
operations reaches three orders of magnitude on a single operation (error propagation) and
an order of magnitude on collection iteration. This doc records the measured cost of each
projection along thirteen interop paths, explains each gap from the projection source and,
where it is disassembly-tractable, from the generated machine code, and lists the concrete
change each projection needs to reach parity with the leader on that path.

The design center: pick a common set of interop operations, hold the component and the call
sequence constant, and vary only the consuming projection. Every difference is then a
property of the projection, not of the workload.

## Scope

In scope: steady-state per-call throughput of the consuming projection on scalar props,
strings, objects, QI, events, delegate add/remove, vector and map iteration, bulk copy,
async await of a completed operation, `IReference<T>` boxing, and error propagation.

Out of scope: startup/first-call latency, binary size, memory working set, and component
authoring cost. NativeAOT is measured for steady-state only; startup — the thing AOT
actually targets — is deliberately not measured here.

## Method

- **Harness**: `microsoft/windows-rs` `crates/samples/lang_perf`, a matrix bench. One
  hand-authored `lang.winmd` defines a no-op `LangPerf.Class`; setters discard, getters
  return fixed values, `Next()` always fails with `E_BOUNDS`. Every loop runs 10,000,000
  iterations. The no-op component means the numbers reflect projection/ABI glue, not
  component work.
- **Consumers** (one process each, identical operation sequence):
  [`rust/src/main.rs`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/samples/lang_perf/rust/src/main.rs),
  [`cpp/src/bench.cpp`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/samples/lang_perf/cpp/src/bench.cpp),
  [`csharp/Program.cs`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/samples/lang_perf/csharp/Program.cs).
- **Components** (no-op, one per language, selected per run):
  [`component/src/lib.rs`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/samples/lang_perf/component/src/lib.rs),
  [`component_cpp/src/component.cpp`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/samples/lang_perf/component_cpp/src/component.cpp).
- **Build flags** (C++/WinRT built for max optimization to keep the comparison fair):
  cppwinrt `-optimize`; `cl /O2 /Ox /GL`; `link /LTCG /OPT:REF /OPT:ICF`. Rust `--release`
  (thin LTO). C#/JIT `dotnet run -c Release`. C#/AOT `dotnet publish -c Release -r win-x64
  /p:PublishAot=true`.
- **Machine**: single desktop, amd64, Rust 1.97.1, .NET 10.0.301, MSVC 14.44, CsWinRT 2.2.0.
  Each run executed in isolation (no two measured processes concurrent).
- **Equivalence**: all thirteen loops were read side by side and confirmed to issue the same
  ABI sequence idiomatically. One apparent asymmetry (Rust `Reference` skipping the
  consumer-side unbox) was investigated and disproved — the windows-rs getter inlines
  `.Value()` inside the projection (see [Reference](#reference-ireferenceint32)).

## Results

Milliseconds for 10,000,000 iterations, consumer projecting the **Rust** component (lower is
better). The matrix confirmed all loops are component-invisible except `Error`, `Async`, and
`Reference`; those are called out separately below.

| Path | Rust | C++/WinRT | C#/JIT | C#/AOT |
| :------------- | -----------: | ------------------: | ---------------------: | ---------------------: |
| Create         |   596 (best) |       658 (+10.4%)  |    12,828 (+2,052.3%)  |     21,440 (+3,497.3%) |
| Int32          |    26 (best) |        30 (+15.4%)  |        65 (+150.0%)    |        118 (+353.8%)   |
| String         |    31 (best) |        50 (+61.3%)  |     1,401 (+4,419.4%)  |        290 (+835.5%)   |
| Object         | 172 (+0.6%)  |       171 (best)    |     1,418 (+729.2%)    |      1,606 (+839.2%)   |
| Cast (QI)      | 350 (+0.6%)  |       348 (best)    |     1,681 (+383.0%)    |      2,853 (+719.8%)   |
| Event          | 369 (+1.4%)  |       364 (best)    |     1,272 (+249.5%)    |      1,391 (+282.1%)   |
| AddRemove      | 1,886 (+3.8%)|     1,817 (best)    |    34,465 (+1,796.8%)  |     68,112 (+3,648.6%) |
| IterateVector  |    15 (best) |       153 (+920.0%) |       550 (+3,566.7%)  |        495 (+3,200.0%) |
| GetMany        | 5 (+150.0%)  |         2 (best)    |       276 (+13,700.0%) |        235 (+11,650.0%)|
| Map            |   792 (best) |     1,008 (+27.3%)  |     2,196 (+177.3%)    |      5,474 (+591.2%)   |
| Async          | 586 (+1.4%)  |       578 (best)    |    52,766 (+9,029.1%)  |    386,319 (+66,737.2%)|
| Reference      |   765 (best) |     2,305 (+201.3%) |    30,825 (+3,929.4%)  |    115,053 (+14,939.6%)|
| Error          |    67 (best) |   247,220 (+368,885.1%) | 25,948 (+38,628.4%) |     42,509 (+63,346.3%) |

Each cell is milliseconds for 10M iterations. The parenthesized percentage is how far above the
fastest projection on that row the cell is — `((cell / row-min) − 1) × 100`, rounded to one
decimal place. `(best)` marks the row winner (0%).

Component-sensitive paths, same consumer, C++ component instead of Rust:

| Path | Rust→C++ | C++→C++ | C#/JIT→C++ | C#/AOT→C++ | driver |
| :-------- | ------------------: | ------------------: | ------------------: | ---------------------: | :----- |
| Async     |     1,195 (+0.2%)   |     1,193 (best)    |   50,911 (+4,167.5%) |    427,355 (+35,721.9%) | component coroutine frame |
| Reference |     3,038 (best)    |     3,693 (+21.6%)  |   31,464 (+935.7%)   |    126,714 (+4,071.0%)  | component-side box |
| Error     |  238,873 (+11.4%)   |   280,176 (+30.7%)  |  214,352 (best)      |    294,524 (+37.4%)     | component originates |

Pure-MSVC C++ build (no cargo, no Rust bin — `cl`/`link` only) reproduces the C++ column and
pushes `Error` higher still: `Error 404,232`, `Reference 3,923`, `IterateVector 160`,
`Async 1,227`. The `Error` number rises because both the component and the consumer are
C++/WinRT and both originate.

## Reading of the matrix

- **Pure-ABI paths tie between the native projections.** `Object`, `Cast`, `Event`, and (for
  a completed operation) `Async` are within noise between Rust and C++/WinRT. Neither native
  projection has structural overhead on a plain vtable crossing.
- **Rust's wins are structural, not incidental.** Every large native gap — `IterateVector`,
  `Reference`, `Error` — traces to a specific windows-rs design choice that C++/WinRT could
  adopt. They are enumerated below with the change each one implies.
- **C#/WinRT pays a per-call managed tax on everything that touches an object, delegate,
  string, or box.** Pure scalar `Int32` (65 ms) is the floor; every allocating path is 20–800x
  the native cost.
- **NativeAOT is worse than JIT on steady-state interop.** `Async` 7x, `Reference` 3.7x,
  `AddRemove` 2x. AOT optimizes startup and size; its generic marshaling stubs do not get the
  JIT's steady-state specialization. `String` is the exception — AOT is 5x *faster* there
  (290 vs 1,401), which points at a portable win (below).

---

## Per-path analysis

### Error (E_BOUNDS propagation)

The headline. Rust→Rust `67 ms` (6.7 ns/call) vs C++/WinRT→C++/WinRT `280,176–404,232 ms`
(28–40 µs/call). The gap is **origination**, not language.

**windows-rs** projects a failed `HRESULT` as an ordinary `Result::Err`. The component
returns
[`Err(E_BOUNDS.into())`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/samples/lang_perf/component/src/lib.rs#L68)
— a bare code. `E_BOUNDS.into()` takes
[`From<HRESULT> for Error`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/libs/result/src/error.rs#L151),
which *reads* thread error info; it never originates. windows-rs originates in exactly one
place —
[`Error::new(code, message)`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/libs/result/src/error.rs#L91)
with a non-empty message, which calls
[`RoOriginateErrorW`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/libs/result/src/error.rs#L296).
Never on receipt, never for a bare code.

**C++/WinRT** originates unconditionally at every throw boundary. cppwinrt's
`hresult_error(code)` ctor calls `originate(...)`, which calls `RoOriginateLanguageException`
([`strings/base_error.h`](https://github.com/microsoft/cppwinrt/blob/master/strings/base_error.h)).
On receipt, `check_hresult` reconstructs an `hresult_error`: if the callee already originated,
it captures and calls `ILanguageExceptionErrorInfo2::CapturePropagationContext`; if not, it
originates. Both are expensive, and both are additive per C++/WinRT endpoint.

**C#/WinRT** projects the failure as a thrown managed exception — ~2.6 µs — cheaper than
C++/WinRT because .NET does not eagerly originate restricted error info and only materializes
a stack when one is read.

Cost decomposition (measured), showing origination is additive per C++/WinRT endpoint:

| combo | originating endpoints | ms @ 10M | per call |
| :---------- | :-------------------- | -------: | -------: |
| Rust→Rust   | none                  |       67 |   6.7 ns |
| Rust→C++    | component only        |  238,873 |  23.9 µs |
| C#/JIT→Rust | consumer throw only   |   25,948 |   2.6 µs |
| C++→C++     | component + consumer  |  280,176 |  28.0 µs |

**Origination cost, isolated.** A Rust variant added to the bench constructs an originating
error each iteration — `Error::new(HRESULT(0x8000_000B), "value")`, which calls
`RoOriginateErrorW` and captures the `IRestrictedErrorInfo`:

```
ErrorOriginate: 118,069 ms   // 11.8 µs/call, vs bare Error 6.7 ns/call -> ~1,760x
```

So the cost is the OS building restricted error info, not the language. Even Rust pays ~11.8 µs
when it opts in — still ~half the C++/WinRT ~24 µs, because windows-rs calls only
`RoOriginateErrorW` with no throw, while C++/WinRT additionally raises and unwinds a real C++
exception and calls the heavier `RoOriginateLanguageException`.

**Disassembly** (symbol build of the C++ component, `cdb`/`uf`). The throw path on every
failed `Next()`:

```
winrt::impl::produce<Class,IClass>::Next        (ABI vtable thunk, 0x80028360)
  call Class::Next                              ; throws hresult_out_of_bounds()
  ...Next'::catch$0   (0x801029f0)              ; SEH funclet catches the C++ exception
    call winrt::to_hresult   (0x80036540)       ; exception -> HRESULT + originate
      call rax                                  ; exception-type dispatch
      call _CxxThrowException  (0x8003658f)     ; rethrow/propagate
```

Link-level confirmation via `dumpbin /imports`:

| binary | `RoOriginateLanguageException` | `RoOriginateErrorW` |
| :----- | :--: | :--: |
| C++ consumer (exe) | yes | — |
| C++ component (`LangPerf.dll`) | yes | — |
| Rust consumer (exe) | — | yes (only from the opt-in variant) |
| **Rust component** (`langperf_rust.dll`) | **—** | **—** |

The Rust component imports neither origination API. It physically cannot originate; that is
the 67 ms.

### IterateVector

Rust `15 ms` vs C++/WinRT `153–160 ms` (~10x), component-independent. **Batching.**

windows-rs iterates via
[`BufferedIterator`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/libs/collections/src/buffered_iterator.rs#L14):
one `GetMany` fills a buffer of
[128 elements](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/libs/collections/src/buffered_iterator.rs#L10)
(`block::<i32>() = (2048/4).clamp(1,128)`), and
[`next`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/libs/collections/src/buffered_iterator.rs#L49)
yields from it. 10M ints cost ~78k ABI crossings. `for x in &v` drives this transparently, so
existing Rust code already gets it.

cppwinrt's range-`for` reads one element per crossing via `IIterator::Current`/`MoveNext`
(the `produce<...vector_impl...>::iterator::MoveNext` thunk is present in the disassembly) —
~10M+ crossings. For `IVector<Int32>`, `GetMany` bulk-copies the values inline, so the
batched loop lands near the explicit `GetMany` number (`5 ms`).

### Map

Rust `792 ms` vs C++/WinRT `1,008 ms` (~27%). Same batching, muted.

windows-rs iterates the map through the same `BufferedIterator`; `IMap` exposes `GetMany`
([`map.rs`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/libs/collections/src/map.rs#L154)),
and the iterator *moves* each `IKeyValuePair` out of the buffer rather than cloning, skipping a
per-pair `AddRef`/`Release`
([`buffered_iterator.rs`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/libs/collections/src/buffered_iterator.rs#L64)).

The win is muted because of what `GetMany` returns. For a vector it is inline `Int32`; for a
map it is a block of `IKeyValuePair` COM objects. Reading each `pair.Value()` is still one
vtable crossing, and the component must allocate a `StockKeyValuePair`
([`key_value_pair.rs`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/libs/collections/src/key_value_pair.rs#L33))
per pair to satisfy the ABI. Those two costs are shared and irreducible, so only the
iterator-stepping crossings are batched away — hence ~27% rather than the vector's 10x.

### String

Rust `31 ms` vs C++/WinRT `50 ms` (~1.9 ns/call). Both are fast-pass reference strings; the
difference is *when* the header is built.

windows-rs
[`h!("value")`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/libs/strings/src/literals.rs#L40)
builds a `static HSTRING` whose `HSTRING_HEADER` (flags `0x11` fast-pass, len, pointer to
compile-time UTF-16) is a `const`. The macro expands to `&RESULT`. Per iteration the set is
one pointer load.

cppwinrt's `StringProperty(L"value")` constructs a `param::hstring` from `wchar_t const*`,
which calls `create_string_reference(value, wcslen(value))` → `create_hstring_on_stack`
([`strings/base_string.h`](https://github.com/microsoft/cppwinrt/blob/master/strings/base_string.h)).
Per iteration it runs `wcslen` and fills a seven-field header on the stack. Small absolute
delta, entirely structural: zero per-call work vs per-call header construction.

### Reference (`IReference<Int32>`)

Rust `765 ms` vs C++/WinRT `2,305 ms` (~3x), each iteration boxing on set and boxing on get.

windows-rs boxes through `windows-reference`: `IReference::<i32>::from` allocates one
in-process `StockReference` that holds just the `i32`, resolving `IPropertyValue` accessors
with a compile-time `TypeId` match. No factory, no combase hop.

cppwinrt `box_value(0)` routes through `reference_traits<int32_t>::make` to
`PropertyValue::CreateInt32`, calling the cached `Windows.Foundation.PropertyValue` activation
factory into combase, where the OS allocates a general `IPropertyValue` carrying the
discriminated-union machinery for all ~20 property types
([`strings/base_value.h`](https://github.com/microsoft/cppwinrt/blob/master/strings/base_value.h)).

The earlier apparent asymmetry — the Rust loop writing only
`let _ = object.ReferenceProperty()?;` — is not real. The windows-rs getter projects
`IReference<Int32>` as `Result<i32>` and calls `.Value()` **inside the projection**
([`rust/src/bindings.rs`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/samples/lang_perf/rust/src/bindings.rs#L163):
`.and_then(|r__: IReference<i32>| r__.Value())`). The bare `?` already returns the unboxed
`i32`. This was caught by building the "fixed" variant and hitting a type error, not by
reading — a reminder that projection code inlines ABI work a static read misses.

### Async, Create, AddRemove — the C#/WinRT and NativeAOT paths

`Async` (await a synchronously-completed `IAsyncOperation<Int32>`) ties between the native
projections at ~580 ms; both short-circuit on `Status() == Completed` and skip the wait. The
number moves with the *component*: the Rust component returns a ready operation
([`IAsyncOperation::<i32>::ready`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/samples/lang_perf/component/src/lib.rs#L103),
an `AtomicBool` + result), while the C++ component `co_return`s. That `co_return` builds a
coroutine whose promise *is* the returned object — a full COM object implementing
`IAsyncOperation` + `IAsyncInfo`, carrying a `slim_mutex`, an agile completed-handler slot, an
atomic status, and a cancel slot, sitting in a heap-allocated coroutine frame that is run to
completion (`initial_suspend` is `suspend_never`) and torn down each call. That frame + promise
machinery is the ~2x, so `→C++` is ~2x `→Rust`.

This gap is a **fixed per-call overhead** (~60 ns/call at this scale), and it only appears
because the operation completes synchronously. If both components did real async work —
`co_await resume_background()`, a thread-pool hop, I/O, then completion — the projections
**converge**, for two reasons. First, the real work (thread-pool dispatch, a context switch,
I/O latency) costs microseconds to milliseconds against the same OS primitives for every
projection, amortizing the ~60 ns frame delta to noise. Second, once the operation genuinely
suspends, Rust can no longer return the trivial `ready` object; it needs a real suspending
future (`windows-future`'s actual state machine), structurally the same shape as the C++
promise, and the consumer's `.get()` blocks on a completion event on both sides instead of
short-circuiting. The coroutine frame is then doing its job, not being wasted. The 2x is
therefore specific to the **synchronously-completed `IAsyncOperation`** pattern — returning an
already-available value through an async-typed API (a cache hit, a fast path, a property
async-typed only for interface uniformity). windows-rs serves that pattern with a dedicated
pre-completed type; C++/WinRT lacks one and pays the coroutine every time. The fix is a
**pre-completed async type** (below), not a change to the coroutine path, which is correct for
genuinely async work.

C#/WinRT trails by two orders of magnitude on everything allocating: `Create` 12,828 (factory
activation + RCW), `AddRemove` 34,465 (a runtime-callable wrapper built and torn down per
subscribe), `Async` 52,766 (await state machine per call), `Reference` 30,825 (nullable box
round-trip). NativeAOT is worse on each — `Async` 386,319, `Reference` 115,053 — because its
generic marshaling stubs are not steady-state-specialized.

---

## Improvements to reach parity

Each item is scoped to one projection and one path, ordered by measured payoff.

### C++/WinRT

1. **Buffered range-`for` iteration** (fixes `IterateVector` 10x, `Map` 27%). Make the
   range-`for` iterator pull via `GetMany` into a small buffer and yield from it, instead of
   one `Current`/`MoveNext` per element — the windows-rs
   [`BufferedIterator`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/libs/collections/src/buffered_iterator.rs)
   design. It is a transparent library change; existing `for (auto&& x : v)` gets the speedup
   with no source edit. Sketch:

   ```cpp
   // cppwinrt fast_iterator: one GetMany per block instead of MoveNext per element.
   template <typename T>
   struct buffered_iterator {
       Windows::Foundation::Collections::IIterator<T> it;
       std::array<T, 128> buf;   // block sized to keep buffer ~1-2 KB
       uint32_t len = 0, idx = 0;
       T const& operator*() const { return buf[idx]; }
       buffered_iterator& operator++() {
           if (++idx >= len) { len = it.GetMany(buf); idx = 0; }
           return *this;
       }
   };
   ```

2. **Opt-out / lazy origination** (fixes `Error`, up to 40 µs/call). Give `hresult_error` a
   mode that does not call `RoOriginateLanguageException` eagerly — originate on first read of
   the error, not at throw. For routine failures (`E_BOUNDS` at end of iteration) provide a
   return-based fast path so the projection never throws. At minimum, document and default
   `WINRT_NO_SOURCE_LOCATION` for release builds to drop the per-throw `std::source_location`
   capture. Reference: cppwinrt
   [`strings/base_error.h`](https://github.com/microsoft/cppwinrt/blob/master/strings/base_error.h).

3. **Compile-time boxed scalars** (fixes `Reference` 3x). Replace the combase round-trip in
   `box_value` for scalar `IReference<T>` with a local stock object holding just the value and
   a compile-time type tag, the windows-rs `StockReference` shape. Keep `PropertyValue` for
   the array/union cases. Reference:
   [`strings/base_value.h`](https://github.com/microsoft/cppwinrt/blob/master/strings/base_value.h).

4. **Compile-time HSTRING literal** (fixes `String`). Add an `h`-style user-defined literal or
   `constexpr` reference-string so `L"value"` interop avoids per-call `wcslen` +
   `create_hstring_on_stack`. Model:
   [`strings/literals.rs`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/libs/strings/src/literals.rs#L40).

   ```cpp
   // Goal: winrt::hstring_ref h = "value"_hs; built once, header is constexpr.
   // Avoids wcslen + create_hstring_on_stack on every call site use.
   ```

5. **Pre-completed async type** (fixes `Async` for the synchronous-result pattern). Add a
   non-coroutine factory that returns an already-completed `IAsyncOperation<T>` holding just
   the value and a `Completed` status — no coroutine frame, no `slim_mutex`, no agile
   completed-handler slot, no `IAsyncInfo` cancel machinery. The analog of windows-rs
   [`IAsyncOperation::<i32>::ready`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/samples/lang_perf/component/src/lib.rs#L103).
   Authors returning an already-available value write the factory instead of `co_return`;
   `co_return` stays correct for genuinely suspending work, where the projection overhead is
   amortized to noise (see [Async](#async-create-addremove--the-cwinrt-and-nativeaot-paths)).

   ```cpp
   // Goal: winrt::make_ready<int32_t>(0) -> completed IAsyncOperation<int32_t>, no coroutine.
   IAsyncOperation<int32_t> Operation() const {
       return winrt::make_ready<int32_t>(0);   // instead of co_return 0;
   }
   ```

### C#/WinRT — JIT

1. **Cache/pool delegate wrappers** (fixes `AddRemove` 34,465). Reuse the runtime-callable
   wrapper across subscribe/unsubscribe of the same managed delegate instead of building and
   tearing one down per call. Reference: `microsoft/CsWinRT` event-source and
   `Marshaler<T>`/delegate ComWrappers.

2. **Fast-path completed async** (fixes `Async` 52,766). When `IAsyncOperation.Status ==
   Completed`, skip the awaiter state machine and call `GetResults()` directly, matching the
   native short-circuit.

3. **Reuse boxed scalars** (fixes `Reference` 30,825). Avoid allocating a new `IReference`
   box per set; cache boxes for hot value types or lower `int?` to a stack marshaler.

4. **Hotter factory cache** (fixes `Create` 12,828). Cache the activation factory per type on
   first use so steady-state `new Class()` is a cached-factory call, not a fresh
   `RoGetActivationFactory`.

### C#/WinRT — NativeAOT

1. **Steady-state-specialized marshaling stubs** (fixes `Async` 386,319, `Reference` 115,053,
   `AddRemove` 68,112). AOT's generic interop stubs are the regression vs JIT. Emit
   type-specialized marshaling at publish time for delegates, async, and boxing rather than
   sharing a generic path. Target: match or beat the JIT column, since AOT removes JIT
   overhead everywhere else.

2. **Port the AOT string win** (`String` AOT 290 vs JIT 1,401). AOT already marshals the
   HSTRING set path 5x cheaper than JIT. Identify that leaner stub and apply the same shape to
   the JIT string marshaler — parity should move *both* directions.

### Rust — windows-rs

windows-rs leads or ties every path; the parity gaps run the other way. Two rough edges to
hold the lead:

1. **`AddRemove`** `1,886` is the one native loop where Rust does not lead (C++/WinRT
   `1,817`). Profile `Event` add/remove for a per-iteration allocation the revoker path could
   avoid. Reference: `crates/libs/core` event/delegate.

2. **`GetMany`** `5` vs C++/WinRT `2`, and `Object` `172` vs `171` — noise-level, no action.

## Reproduction

Full step-by-step — prerequisites, what to clone where, what to build, what to run, expected
output, and the optional disassembly — is in [`REPRODUCE.md`](./REPRODUCE.md). In short:

```powershell
git clone https://github.com/jonwis/windows-rs C:\src\windows-rs   # short path, not OneDrive
cd C:\src\windows-rs; git checkout jonwis/perf-bench
cd crates\samples\lang_perf
.\run.ps1 -Iterations 10000000 -IncludeAot     # full matrix; use 1000000 to iterate faster
.\msvc\build.ps1 -Iterations 10000000          # pure-MSVC C++ build (no cargo, no Rust)
```

Harness changes made for this comparison (max-opt C++ flags, the pure-MSVC build, the Rust
`ErrorOriginate` variant) are logged in
`crates/samples/lang_perf/CHANGES-perf-analysis.md` in the windows-rs tree.

## Sources / links

- Bench harness (pinned): [`crates/samples/lang_perf`](https://github.com/microsoft/windows-rs/tree/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/samples/lang_perf) @ `a4f9241`.
- windows-rs projection internals: [`buffered_iterator.rs`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/libs/collections/src/buffered_iterator.rs), [`result/src/error.rs`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/libs/result/src/error.rs), [`strings/src/literals.rs`](https://github.com/microsoft/windows-rs/blob/a4f924122bcdc1e65b94e882b5ea874cccad23bb/crates/libs/strings/src/literals.rs).
- C++/WinRT: [`microsoft/cppwinrt`](https://github.com/microsoft/cppwinrt) — `strings/base_error.h`, `strings/base_string.h`, `strings/base_value.h`.
- C#/WinRT: [`microsoft/CsWinRT`](https://github.com/microsoft/CsWinRT).
