# C++/WinRT RuntimeClass Interface Caching Experiments

## Problem

C++/WinRT projects a WinRT runtimeclass as a single C++ object wrapping the *default interface* pointer. Calling a method defined on a non-default interface (e.g. `IMap::Insert` through a `PropertySet` which defaults to `IPropertySet`) requires a `QueryInterface` call, the vtable dispatch, and a `Release` — on **every call**. For runtimeclasses with many interfaces (`StorageFile` has 10+), this means repeated QI/Release overhead at every call site.

## Goal

Cache non-default interface pointers so the QI cost is paid once per interface, then all subsequent calls dispatch directly through the cached pointer's vtable.

## Approaches Explored

| Namespace | Technique | Result |
|-----------|-----------|--------|
| `use_cached_directly` | `std::tuple<IFace...>` with explicit `ensure_interface<T>()` | Works, but every method manually calls ensure + get |
| `virtual_wrapper` | C++ virtual class wrapping the cached tuple | Hides caching behind vtable, but adds shared_ptr overhead |
| `cppwinrt_cheese` | Derive from projected type, `static_cast` to force interface | Simplest, but no caching — still QIs per call |
| `another_attempt` | `void*` array + raw QI via IID | Compact, inlines well, but hand-rolled per type |
| `winrt::fast` | **Thunked base class + MASM stubs** | Generic, minimal binary size, thread-safe |

## The Thunk Solution (`winrt::fast`)

The thunk infrastructure lives in `winrt::fast::impl`. Projected thunked types mirror
the standard WinRT namespace under a `fast` sub-namespace, e.g.
`winrt::Windows::Foundation::Collections::fast::PropertySet`.

### Architecture

```
PropertySet object layout:
┌──────────────────────────────────┐
│ cache[0]: IPropertySet*  (real)  │ ← default interface, always resolved
│ cache[1]: void*                  │ ← initially &thunks[0], replaced with IMap* on first use
│ cache[2]: void*                  │ ← initially &thunks[1], replaced with IIterable*
│ cache[3]: void*                  │ ← initially &thunks[2], replaced with IObservableMap*
├──────────────────────────────────┤
│ thunks[0]: InterfaceThunk       │ ← fake COM object for IMap
│   .vtable → g_thunk_vtable      │
│   .default_abi → cache[0]       │
│   .cache_slot → &cache[1]       │
│   .iid → IID_IMap               │
├──────────────────────────────────┤
│ thunks[1]: InterfaceThunk       │ ← fake COM object for IIterable
│ thunks[2]: InterfaceThunk       │ ← fake COM object for IObservableMap
└──────────────────────────────────┘
```

Each `InterfaceThunk` has a vtable pointer as its first field, making it look like a COM object. The vtable points into a shared table of 256 MASM-generated stubs. Each stub is 10 bytes (x64):

```asm
winrt_fast_thunk_stub_82:
    mov     eax, 52h                ; slot index
    jmp     common_thunk_dispatch   ; shared dispatch
```

`common_thunk_dispatch` (60 bytes, shared across all stubs):
1. Saves caller's `rdx`/`r8`/`r9` in shadow space
2. Calls `winrt_fast_resolve_thunk()` with `rcx` = `InterfaceThunk*`
3. `resolve()` atomically replaces `*cache_slot` with the real interface via `compare_exchange_strong`
4. Loads `real_vtable[slot_index]` and tail-jumps to the real method

After resolution, the cache slot points directly to the real COM interface. All subsequent calls through `map_iface().Insert(...)` go straight to the real vtable — the thunk is never touched again.

### Thread Safety

`resolve()` uses `compare_exchange_strong` on the atomic cache slot. If two threads race to resolve the same interface, both QI successfully, the loser releases its result and uses the winner's pointer. No locks.

### Binary Size

| Component | x64 | x86 | ARM64 |
|-----------|-----|-----|-------|
| Per stub | 10 bytes | 7 bytes | 8 bytes |
| 256 stubs | 2,560 | 1,792 | 2,048 |
| Common dispatch | 60 | ~50 | ~60 |
| Vtable array | 2,048 | 1,024 | 2,048 |
| **Total** | **~4.7 KB** | **~2.9 KB** | **~4.2 KB** |

This is shared across *all* thunked runtimeclasses. Adding a new type costs zero additional code — just the per-instance `cache[]` + `thunks[]` storage.

### Base Class

`ThunkedRuntimeClass<IDefault, I...>` is templated on the default and secondary interfaces.
It owns the cache array, thunk objects, and provides copy/move/destroy semantics.
Derived types alias the base and forward constructors:

```cpp
namespace winrt::Windows::Foundation::Collections::fast
{
    struct PropertySet : protected winrt::fast::impl::ThunkedRuntimeClass<
        IPropertySet,
        IMap<hstring, IInspectable>,
        IIterable<IKeyValuePair<hstring, IInspectable>>,
        IObservableMap<hstring, IInspectable>>
    {
        using base_t = winrt::fast::impl::ThunkedRuntimeClass<...>;
        PropertySet() : PropertySet(winrt::detach_abi(...), winrt::take_ownership_from_abi) {}
        PropertySet(nullptr_t) : base_t(nullptr) {}
        PropertySet(void* p, take_ownership_from_abi_t) : base_t(p) {}
        PropertySet(PropertySet const&) = default;
        // ...
    };
}
```

## Codegen Comparison

Two identical functions — create a `PropertySet`, insert 3 values, call `GetView()`:

### Thunked: `Insert` callee (63 bytes)

```asm
; Direct vtable call — cache slot already holds IMap* (or thunk on first call)
sub     rsp, 38h
mov     rcx, [rcx]          ; load interface pointer from cache slot
mov     rax, [rcx]           ; vtable
call    [rax+50h]            ; IMap::Insert vtable slot
; ... error check, return
ret
```

### C++/WinRT: `Insert` callee (123 bytes)

```asm
; Must QI for IMap on every call, then Release after
sub     rsp, 30h
mov     rdx, [rcx]           ; load IPropertySet*
lea     rcx, [rsp+48h]
call    try_as<IMap>          ; QueryInterface for IMap — every time
mov     rcx, [rsp+48h]
mov     rax, [rcx]
call    [rax+50h]             ; IMap::Insert
call    unconditional_release ; Release the QI'd IMap*
; ... error check, return
ret
```

The thunked version eliminates the per-call QI+Release entirely. The first call pays the QI cost through the thunk; all subsequent calls dispatch directly. The C++/WinRT version pays QI+Release on **every** call to any non-default interface method.

### GetView comparison

| | Thunked | C++/WinRT |
|---|---|---|
| Callee size | 71 bytes | 105 bytes |
| QI per call | 0 (after first) | 1 |
| Release per call | 0 | 1 |

## Files

| File | Purpose |
|------|---------|
| `experiment.cpp` | Main entry point + side-by-side comparison functions |
| `thunk_experiment.h` | `winrt::fast::impl` thunk infrastructure + `winrt::...::fast::PropertySet` |
| `self_replacing.cpp` | Sketch of alternative self-replacing trampoline design (`#if 0`) |
| `cached_types.cpp` | Additional cached type experiments |
| `cppwinrt_replicant.cpp` | Replicant projection experiments |
| `shared.h` | Common type aliases and `comparison<T>()` template |
| `thunk_stubs.asm` | x64 MASM stubs + vtable array (`winrt_fast_thunk_stub_*`) |
| `thunk_stubs_x86.asm` | x86 MASM stubs (stdcall-compatible, 4-byte vtable slots) |
| `thunk_stubs_arm64.asm` | ARM64 armasm64 stubs |
| `thunk_stubs_arm64ec.asm` | ARM64EC stubs (identical logic to ARM64) |
| `thunk_tests.cpp` | Correctness, lifecycle, pass-by-ref/value, 8-thread concurrent resolve |

## Building

```powershell
# x64
cmake --preset vsbuild
cmake --build --preset vsbuild-release --target cppwinrt-proj
```

## Testing

```powershell
# Run via ctest
ctest --test-dir out/build/vsbuild

# Or run directly
.\out\build\vsbuild\code\cppwinrt-proj\RelWithDebInfo\cppwinrt-proj.exe

# Run under debugger (catch AVs)
c:\debuggers\cdb.exe -g -G -c "sxe av ; g ; q" .\out\build\vsbuild\code\cppwinrt-proj\RelWithDebInfo\cppwinrt-proj.exe
```

## Compatibility: Breaking the Single-Pointer Assumption

C++/WinRT's core invariant is that a projected runtimeclass *is* its default interface pointer. `PropertySet` in cppwinrt is `sizeof(void*)` — it's literally an `IPropertySet*` in a wrapper. This means:

- `winrt::get_abi(ps)` returns the raw `IPropertySet*`
- `winrt::attach_abi(ps, ptr)` / `winrt::detach_abi(ps)` work by moving the single pointer
- Passing a `PropertySet` to a function expecting `IPropertySet const&` is a zero-cost reinterpret
- `reinterpret_cast<IPropertySet*>(&ps)` is valid

The thunked `PropertySet` breaks all of this. It's ~128 bytes (cache array + thunk objects + iids pointer), not 8. `winrt::get_abi` won't compile. Passing it where an `IPropertySet` is expected requires an explicit conversion operator, not a reinterpret.

### Integration Path for C++/WinRT

If this technique were adopted into cppwinrt's code generator:

1. **Keep the single-pointer projected type unchanged.** `PropertySet` remains `sizeof(void*)` and satisfies all existing ABI contracts. The thunked cache lives in a *separate* opt-in wrapper type under a `fast` sub-namespace (e.g. `winrt::Windows::Foundation::Collections::fast::PropertySet`).

2. **`winrt::...::fast::PropertySet` as the thunked wrapper.** Contains a `ThunkedRuntimeClass<IDefault, I...>` where the interface list is computed from the metadata. Provides all the same methods as the standard projected type. Implicit conversion to the standard projected type (extracts default interface, AddRefs). This preserves API compatibility — anything that takes `PropertySet` by value/ref still works.

3. **`get_abi` / `detach_abi` support.** The thunked type exposes `get_abi()` returning the default interface pointer. `detach_abi()` detaches the default and clears the cache. This maps cleanly to the existing ABI contract.

4. **Opt-in per type at point of use.** No metadata or projection changes needed. A developer writes `fast::PropertySet ps;` instead of `PropertySet ps;` when they want caching. The projected `PropertySet` type is unchanged.

5. **Code generator could emit the IID table and method forwarding.** The `iids[]` array, slot indices, and typed accessor methods (`map_iface()`, etc.) are all derivable from the `.winmd` metadata. The thunk stubs and `ThunkedRuntimeClass<N>` base are shared infrastructure — they don't change per type.

6. **Alternative: transparent caching in the projected type itself.** Instead of a wrapper, cppwinrt could change `PropertySet` to contain a pointer to a heap-allocated cache block (allocated on first non-default QI). This preserves `sizeof(void*)` for the type itself at the cost of one extra indirection + heap allocation on first non-default call. The cache block would be shared across copies via ref-counting or simply re-created (since QI is idempotent).
