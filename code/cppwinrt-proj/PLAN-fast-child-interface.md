# Plan: Integrate Fast Child Interface Thunks into C++/WinRT

## Summary

Change the **default projection** of WinRT runtimeclasses so they use the thunk-based
interface caching system from `winrt::fast::impl`. Instead of `PropertySet` being `sizeof(void*)`
wrapping the default interface pointer, it becomes a larger type with:
- A `runtimeclass_header` containing the default interface pointer
- Per-secondary-interface cache+thunk pairs that lazily QI on first use

The runtimeclass exposes a `get_default()` method returning a reference to the default
interface, and all ABI interop flows through that. Functions accepting runtimeclass-typed
parameters marshal them via `winrt::get_abi(v)` which calls `v.get_default()` to extract
the ABI-safe default interface pointer.

The thunk infrastructure lives in `winrt::fast::impl`, while projected thunked types
mirror the standard WinRT namespace hierarchy under a `fast` sub-namespace
(e.g. `winrt::Windows::Foundation::Collections::fast::PropertySet`).

---

## Design Constraints

1. **All existing tests must compile and pass unchanged.** No modifications to test code.
2. **`winrt::get_abi()` / `put_abi()` / `attach_abi()` / `detach_abi()` continue to work** — they just go through `get_default()` instead of aliasing `&object` as `void**`.
3. **Generated consume methods still work** — `consume_general` already QIs when `D != Base`, and the QI result is a projected interface (still `sizeof(void*)`).
4. **Interop:** A runtimeclass is passable to any function taking any of its interfaces by `const&`, or `IInspectable const&`.

---

## Why This Can Work: The `consume_general` Invariant

The existing consume dispatch already handles the runtimeclass case correctly:

```cpp
template <typename Base, typename Derive, typename MemberPointer, typename ...Args>
void consume_general(Derive const* d, MemberPointer mptr, Args&&... args)
{
    if constexpr (!std::is_same_v<Derive, Base>)
    {
        // D is a runtimeclass, Base is an interface → QI
        auto const result = try_as_with_reason<Base>(d, code);
        auto const abi = *(abi_t<Base>**)&result;  // result is sizeof(void*) ✓
        check_hresult((abi->*mptr)(args...));
    }
    else
    {
        // D IS the interface → direct alias
        auto const abi = *(abi_t<Base>**)d;  // d is sizeof(void*) ✓
        check_hresult((abi->*mptr)(args...));
    }
}
```

When `D` is a runtimeclass (e.g. `PropertySet`) and `Base` is any interface (e.g. `IMap`),
`D != Base` is always true → takes the QI branch. The QI result is a projected **interface**
type, which remains `sizeof(void*)`. The `*(abi_t<Base>**)&result` aliasing operates on
that temporary interface, not on the runtimeclass.

The only branch doing `*(abi_t<Base>**)d` is the `D == Base` case, which only fires when
calling a method directly on an interface type — never on a runtimeclass.

**Therefore:** `consume_general` does not need to change. The runtimeclass can be any size,
as long as `try_as_with_reason<Base>(d, ...)` can extract the default ABI pointer from `d`
to perform QI. That function calls `d->try_as<Base>()` → `get_abi(*d)` internally →
which is where `get_default()` hooks in.

---

## Key Technical Approach: `get_default()` and `get_abi()` Override

### The `get_default()` Method

Every Cached runtimeclass exposes:

```cpp
IDefault const& get_default() const noexcept
{
    return *reinterpret_cast<IDefault const*>(&default_cache);
}
```

This returns a reference to the `default_cache` member (which holds the raw ABI pointer),
reinterpreted as the default interface type. This is valid because projected interfaces are
`sizeof(void*)` and layout-compatible with a raw pointer.

### Overriding `get_abi()` for Runtimeclasses

Today in `base_windows.h`:
```cpp
inline void* get_abi(IUnknown const& object) noexcept
{
    return *(void**)(&object);
}
```

This works for `sizeof(void*)` types. For Cached runtimeclasses, we generate per-class
overloads that ADL finds before the base overload:

**Per-class `get_abi` overload (generated in `*.2.h`):**
```cpp
inline void* get_abi(PropertySet const& object) noexcept
{
    return get_abi(object.get_default());
}
```

The base `get_abi(IUnknown const&)` stays unchanged for interfaces.

### `put_abi()` for Runtimeclasses

```cpp
inline void** put_abi(PropertySet& object) noexcept
{
    object = nullptr;
    return reinterpret_cast<void**>(&object.default_cache);
}
```

Returns a pointer to the `default_cache` member so COM APIs can write directly into it.

### `write_abi_args` Change — The Critical Code Generator Fix

**FINDING: Cannot simply change `*(void**)(&param)` to `get_abi(param)`.** The generic
`get_abi<T>` template in `base_windows.h` has a `std::is_base_of_v<IUnknown, T>` SFINAE
guard. When the compiler evaluates overload candidates, it eagerly instantiates this
trait even for types that would match a more specific overload. In template contexts
where `param::async_vector_view<T>`, `param::async_iterable<T>`, etc. are incomplete
types, this SFINAE check fails with `C2139: undefined class not allowed as argument
to __is_base_of`.

This affects `object_type` params because that category includes both IUnknown-derived
types (interfaces, delegates, runtimeclasses) AND `param::` collection wrapper types
(which are NOT IUnknown-derived). The `string_type` category triggers the same issue
because `hstring` params appear in template contexts where other types are incomplete.

**Approach:** The `*(void**)(&param)` pattern must stay in `write_abi_args` for now.
When we change the runtimeclass layout, the forwarding methods on the runtimeclass
will bypass `consume_general` entirely — they cast `*this` to the cached interface
reference, then call the interface's method directly. The ABI args in that path
operate on the **interface** type (still `sizeof(void*)`), so `*(void**)(&param)`
remains correct. The `write_abi_args` change is NOT needed for the thunk approach.

```diff
 // code_writers.h — NO CHANGE needed:
 case param_category::object_type:
 case param_category::string_type:
     w.write("*(void**)(&%)", param_name);  // unchanged
     break;
```

### `bind_out` — No Change Needed

**FINDING: `bind_out` cannot use `put_abi(object)` either.** The `bind_out` template
is instantiated with `param::async_vector_view<T>` and similar incomplete types.
Any `is_base_of_v<IUnknown, T>` check in `bind_out::operator void**()` triggers the
same SFINAE failure. The original `(void**)(&object)` pattern is correct because
`bind_out` is only used for OUT params, which always write a single COM pointer into
the location.

### `base_fast_abi.h` — Confirmed Working

The `runtimeclass_header`, `interface_thunk`, `runtimeclass_cache_base`, and
`runtimeclass_base<IDefault, I...>` types compile cleanly in `winrt/base.h`.
Uses `unknown_abi` (not `::IUnknown`) for all COM operations to avoid SDK header
dependencies. All existing tests pass with `base_fast_abi.h` included.

---

## How Parameters Work

### `Use(PropertySet const& v)` — Calling an ABI method with a runtimeclass param

The consume methods in cppwinrt are CRTP mixins on the **interface** type. When a
runtimeclass's forwarding method calls through a cached interface reference:

```cpp
// Generated forwarding method on PropertySet:
auto Insert(param::hstring const& key, IInspectable const& value) const
{
    return static_cast<IMap<hstring, IInspectable> const&>(*this)
        .Insert(static_cast<hstring const&>(key), value);
}
```

The `.Insert()` call resolves to `consume_Windows_Foundation_Collections_IMap<IMap<K,V>, K, V>::Insert`,
where `D == IMap<K,V>` (the interface itself). The consume method then calls:

```cpp
consume_general<IMap<K,V>, IMap<K,V>>(this, &abi_t<IMap>::Insert, *(void**)(&key), *(void**)(&value), &result);
```

Since `D == Base`, it takes the direct vtable path — no QI. The `*(void**)(&param)` pattern
operates on the **interface reference** in the cache slot (which is `sizeof(void*)` —
either the real pointer or the thunk). This is correct. No `write_abi_args` change needed.

### Passing a `PropertySet` to a function expecting `IMap<K,V> const&`

With thunking, the conversion operator returns a reference to the cached interface slot
instead of doing QI:

```cpp
// In runtimeclass_base:
operator IMap<hstring, IInspectable> const&() const noexcept
{
    return *reinterpret_cast<IMap<...> const*>(&pairs[0].cache);
}
```

The cache slot either holds the real `IMap*` (after first resolution) or the thunk pointer
(which transparently resolves on first method call via the ASM stubs). Zero-cost after
first use.

---

## Runtimeclass Generated Shape

### Before (current cppwinrt):

```cpp
struct WINRT_IMPL_EMPTY_BASES PropertySet : IPropertySet
{
    PropertySet(std::nullptr_t) noexcept {}
    PropertySet(void* ptr, take_ownership_from_abi_t) noexcept
        : IPropertySet(ptr, take_ownership_from_abi) {}
    PropertySet();
};
```

`sizeof(PropertySet) == sizeof(void*)`. No data members. All non-default interface methods
come from `require<PropertySet, IMap<...>, ...>` CRTP mixins which QI on every call.

### After (Cached):

```cpp
struct PropertySet : impl::runtimeclass_base<IPropertySet,
    IMap<hstring, IInspectable>,
    IIterable<IKeyValuePair<hstring, IInspectable>>,
    IObservableMap<hstring, IInspectable>>
{
    PropertySet(std::nullptr_t) noexcept {}
    PropertySet(void* ptr, take_ownership_from_abi_t) noexcept
        : runtimeclass_base(ptr) {}
    PropertySet();

    // Conversion operators (inherited from runtimeclass_base):
    //   operator IPropertySet const&()     → &default_cache
    //   operator IMap<...> const&()        → &pairs[0].cache (thunk-backed)
    //   operator IIterable<...> const&()   → &pairs[1].cache (thunk-backed)
    //   operator IObservableMap<...> const&() → &pairs[2].cache (thunk-backed)

    // get_default() for ABI interop:
    IPropertySet const& get_default() const noexcept { ... }

    // Explicit forwarding methods (replace require<> CRTP methods):
    auto Size() const { return static_cast<IMap<hstring, IInspectable> const&>(*this).Size(); }
    auto HasKey(param::hstring const& key) const { ... }
    auto Lookup(param::hstring const& key) const { ... }
    auto Insert(param::hstring const& key, IInspectable const& value) const { ... }
    auto Remove(param::hstring const& key) const { ... }
    auto Clear() const { ... }
    auto GetView() const { ... }
    auto First() const { return static_cast<IIterable<...> const&>(*this).First(); }
    auto MapChanged(handler) const { ... }
    void MapChanged(event_token) const noexcept { ... }
};

// ABI interop overloads (generated alongside the class):
inline void* get_abi(PropertySet const& object) noexcept { return get_abi(object.get_default()); }
inline void** put_abi(PropertySet& object) noexcept { ... }
inline void* detach_abi(PropertySet& object) noexcept { ... }
inline void attach_abi(PropertySet& object, void* value) noexcept { ... }
```

**Why explicit forwarding methods?** Each method casts `*this` to the cached interface
reference, then calls the method on the **interface type** directly. This means
`consume_general` sees `D == Base` (interface calling its own method) and takes the
direct vtable path — no QI. On first call through the thunk, the ASM stubs transparently
resolve the real interface pointer. After that, all calls are direct vtable dispatch.

**Why no `require<>` inheritance?** The forwarding methods provide all the same methods.
`require<>` would add redundant CRTP methods that go through the QI path. Without it,
the class is cleaner and every method call is guaranteed to use the cached path.

---

## Implementation Plan

### Phase 1: Runtime Infrastructure (strings/)

#### 1.1 — `base_fast_abi.h` (new file in `strings/`)

Contains (in `winrt::fast::impl` namespace):
- `ThunkedRuntimeClassHeader` (16 bytes, `alignas(16)`)
- `InterfaceThunk` (16 bytes) with `resolve()` logic
- `CacheAndThunkTagged` / `CacheAndThunkFull` pair types
- `init_pair_tagged()` / `init_pair_full()` helpers
- `ThunkedRuntimeClass` base with `clear_impl`, `attach_impl`, `copy_from`, `move_from`
- `ThunkedRuntimeClass<IDefault, I...>` typed template
- `type_index<T, Types...>` helper
- `extern "C"` declarations for `winrt_fast_resolve_thunk` and `winrt_fast_thunk_vtable`

#### 1.2 — Assembly stubs (new files)

| File | Architecture | Shared cost |
|------|-------------|-------------|
| `strings/fast_abi/thunk_stubs_x64.asm` | x64 (`winrt_fast_thunk_stub_*`) | ~4.7 KB |
| `strings/fast_abi/thunk_stubs_x86.asm` | x86 (`winrt_fast_thunk_stub_*`) | ~2.9 KB |
| `strings/fast_abi/thunk_stubs_arm64.asm` | ARM64 (`winrt_fast_thunk_stub_*`) | ~4.2 KB |
| `strings/fast_abi/thunk_stubs_arm64ec.asm` | ARM64EC (`winrt_fast_thunk_stub_*`) | ~4.2 KB |

#### 1.3 — Build system & base header integration

- New `cppwinrt_fast_abi` static library target in `CMakeLists.txt`
- Include `base_fast_abi.h` in `write_base_h()` after `base_implements.h`

### Phase 2: ABI Interop Layer Changes

#### 2.1 — `write_abi_args` (code_writers.h)

`*(void**)(&param)` → `get_abi(param)` for `object_type` IN params.

#### 2.2 — `bind_out` (base_string.h)

`(void**)(&object)` → `put_abi(object)`.

#### 2.3 — No changes to `consume_general`, `consume_noexcept`, `get_abi(IUnknown const&)`

### Phase 3: Code Generation — Runtimeclass Shape

#### 3.1 — `write_Cached_class()` (new function in code_writers.h)

Replaces `write_slow_class()` for runtimeclasses. Generates:
- Inheritance from `winrt::fast::impl::ThunkedRuntimeClass<IDefault, I...>` (no `require<>`)
- `get_default()` method
- Explicit forwarding methods for all interface methods
- Constructors (nullptr, take_ownership_from_abi, activation)
- A `base_t` alias for the ThunkedRuntimeClass specialization

#### 3.2 — `write_class_abi_overloads()` (new function in code_writers.h)

Generates per-class `get_abi`, `put_abi`, `detach_abi`, `attach_abi` overloads.

#### 3.3 — Method forwarding generation

For each method on each interface of the runtimeclass:
```cpp
auto MethodName(param types...) const
{
    return static_cast<InterfaceName const&>(*this).MethodName(args...);
}
```

**`param::hstring` and other `param::` wrappers must be explicitly cast** when forwarding.
The `param::hstring` type is non-copyable — it captures a `wchar_t const*` by reference
and constructs a stack-based `HSTRING_HEADER`. If you pass `param::hstring const& key`
directly to the target interface's consume method (which also takes `param::hstring const&`
via `impl::param_type<hstring>`), the compiler may attempt an intermediate conversion
through `param::hstring(winrt::hstring const&)` → deleted copy. The fix: cast to the
underlying projected type first:

```cpp
// WRONG — may hit deleted copy through param::hstring intermediate:
auto Lookup(param::hstring const& key) const
{
    return static_cast<IMap<hstring, IInspectable> const&>(*this).Lookup(key);
}

// CORRECT — cast param::hstring → winrt::hstring const& first:
auto Lookup(param::hstring const& key) const
{
    return static_cast<IMap<hstring, IInspectable> const&>(*this)
        .Lookup(static_cast<winrt::hstring const&>(key));
}
```

This applies to all `param::` wrapper types (`param::hstring`, `param::async_iterable<T>`,
`param::iterable<T>`, `param::vector_view<T>`, `param::map_view<K,V>`). Each has an
`operator ProjectedType const&()` conversion that the `static_cast` invokes. The code
generator must emit the explicit cast for every parameter whose `param_category` maps
to a `param::` wrapper type.

The prototype in `thunk_experiment.h` already demonstrates this pattern:
```cpp
auto HasKey(winrt::param::hstring key) const {
    return static_cast<IMap<winrt::hstring, IInspectable> const&>(*this)
        .HasKey(static_cast<winrt::hstring const&>(key));
}
```

Handle name collisions the same way existing cppwinrt handles them.

### Phase 4: Test Compatibility

#### 4.1 — Existing tests pass unchanged

| Pattern | Why it works |
|---------|-------------|
| `get_abi(runtimeclass)` | Generated overload → `get_default()` → correct ABI pointer |
| `put_abi(runtimeclass)` | Generated overload → `&default_cache` |
| `detach_abi(runtimeclass)` | Generated overload → extracts from `default_cache` |
| `ps.Insert(...)` | Forwarding method → cached interface → direct vtable call |
| `ps.as<IFoo>()` | `runtimeclass_cache_base::as()` → QI through default interface |
| `IMap<K,V> map = ps;` | Conversion operator → cached slot reference → lazy thunk |
| `Nomadic.cpp` vtable walking | `get_abi(nomadic)` → generated overload → correct ABI pointer |

#### 4.2 — New test target (`test/test_fast_abi/`)

Tests: cached dispatch, copy/move, conversions, thread safety, >8 interfaces, ABI round-trips.

---

## File Change Summary

### New files

| File | Purpose |
|------|---------|
| `strings/base_fast_abi.h` | Thunk runtime infrastructure |
| `strings/fast_abi/thunk_stubs_{x64,x86,arm64,arm64ec}.asm` | Assembly stubs |
| `test/test_fast_abi/` | New tests |

### Modified files

| File | Change |
|------|--------|
| `cppwinrt/code_writers.h` | New `write_thunked_class()`, `write_class_abi_overloads()`. NO change to `write_abi_args`. |
| `cppwinrt/file_writers.h` | Include `base_fast_abi.h`. Call `write_thunked_class()` instead of `write_slow_class()`. |
| `CMakeLists.txt` | New `cppwinrt_fast_abi` library target |

### Removed

| Item | Reason |
|------|--------|
| `InMemoryRandomAccessStream` thunked type | Removed to focus prototype on collection types (PropertySet). Stream types can be added later. |

### Unchanged

- All existing test files
- `strings/base_windows.h` — `get_abi(IUnknown const&)` stays as-is
- `strings/base_string.h` — `bind_out` stays as-is
- `cppwinrt/code_writers.h` `write_abi_args` — `*(void**)(&param)` stays as-is
- `consume_general` / `consume_noexcept` — unchanged
- `WINRT_IMPL_SHIM` — unchanged
- `strings/base_meta.h` — `require<>` unchanged (just not used by Cached classes)

---

## Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| 256-slot vtable limit | WinRT interfaces rarely exceed ~30 methods. `static_assert` in generated code. |
| Per-instance memory | 88 bytes (PropertySet, 3 secondary) vs 8 bytes. QI elimination justifies it. |
| `*(void**)(&param)` in consume code | Stays unchanged — forwarding methods bypass consume_general entirely. ABI args operate on cached interface refs which are `sizeof(void*)`. |
| No `require<>` on runtimeclass | All methods explicitly forwarded. Same API surface. |
| `factory_cache_entry` / `when_any` atomics | Operate on interface types, not runtimeclasses. Unaffected. |
| Binary size of thunk stubs | ~4.7 KB shared across ALL types. Negligible. |

---

## Open Questions

1. **`put_abi` and thunk reinitialization:** When COM writes a new pointer through `put_abi`,
   secondary thunks are stale. Options: auto-reinit wrapper, explicit `reinit_thunks()`,
   or smarter `put_abi` with post-write hook.

2. **Classes with 0 secondary interfaces** (e.g. `Deferral`): Special-case to keep
   `sizeof(void*)` layout? Or always use `runtimeclass_base<IDeferral>` with N=0
   (16 bytes for the header, no pairs)?

3. **NuGet packaging:** ASM stubs need compilation. Pre-compiled `.obj` per arch,
   source `.asm` with MSBuild integration, or inline asm (not portable).

4. **MinGW support:** MASM stubs need GAS equivalents.

5. **Composable classes:** Classes with `base<>` relationships (e.g. `Control` → `UIElement`)
   need the Cached type to compose correctly with the base class's Cached type.
