# RuntimeClass Composition

WinRT’s RuntimeClass system allows a very attractive "all the methods are
visible on this object you have" developer experience. Intellisense and other
code-authoring helpers merge all the methods and their overloads into a single
object that the developer "dots through" at runtime. While this is conceptually
good, the runtime implication of dealing with multiple interfaces comes with a
high binary and wall-clock-execution time cost.

## Use the Least Derived Interface for common calls

Consider [https://learn.microsoft.com/uwp/api/windows.data.json.jsonobject](the
`Windows.Data.Json.JsonObject` type.) It is a composite of multiple interfaces –
`IJsonObject` (the default), `IInspectable` (inherited), `IJsonValue`,
`IMap<String, IJsonValue>`, `IIterable<IKeyValuePair<String, IJsonValue>>`,
IIterable<IKeyValuePair<String, IJsonValue>>, and IStringable. Using any method
outside of JsonObject (IJsonObject) requires a QueryInterface to the target
interface. Consider this normal-looking code:

```c++
ValueSet v;
v.Insert("Kittens", box_value(true));
v.Insert("Puppies", box_value<uint32_t>(15));
v.Insert("Hamster", box_value(L"Trundle"));
```

Every call to "Insert" hides a `QueryInterface` from `IValueSet` (a marker
interface with no other methods) to `IMap<String, Object>`. `ValueSet` is a
platform-provided type so there is no hope of the compiler figuring out that the
`QueryInterface` calls can be merged.

An easy way to enhance the performance is a one-time conversion so that only one
QueryInterface is performed:

```c++
ValueSet v;
IMap<hstring, IInspectable> m { v }; // QI from IValueSet -> IMap<K,V>
m.Insert("Kittens", box_value(true));
m.Insert("Puppies", box_value<uint32_t>(15));
m.Insert("Hamster", box_value("Trundle"));
```

The downside of this operation is that it makes the code look less "good" –
rather than just using `ValueSet::Insert` as is projected by C++/WinRT and C#
and Rust, the developer manually inserts the conversion.

-   Types that are the most susceptible to this problem include:
-   Types that are collections (`PropertySet`, `ValueSet`, `StringMap`, etc)
-   Types that implement ("require") Windows.Foundation.Collections interfaces
-   Types using `IClosable`, `IStringable`
-   Deeply nested hierarchies (closed or open) like XAML and Composition

## If necessary, directly use Statics interfaces

Related to the above "many query interfaces" issue, invoking static methods in
sequence in a method generates many individual calls to the static factory cache
for your protection. A common example is lurking in the above example for
ValueSet. Each call to `box_value<>` is eventually a call to
`winrt::Windows::Foundation::PropertyValue::Create...` static methods to
instantiate the reference wrapper. Each static method call looks for the statics
value in the module statics cache, acquiring it if not present. Consider this:

```c++
ValueSet v;
IMap<hstring, IInspectable> m { v };
m.Insert("Kittens", PropertyValue::CreateBoolean(true));
m.Insert("Puppies", PropertyValue::CreateUInt32(15));
m.Insert("Hamster", PropertyValue::CreateString("Trundle"));
```

Instead, consider manually fetching the static interface for the type once and
using it repeatedly:

```c++
ValueSet v;
IMap<hstring, IInspectable> m { v };
auto s = get_activation_factory<winrt::PropertyValue, winrt::IPropertyValueStatics>();
m.Insert("Kittens", s.CreateBoolean(true));
m.Insert("Puppies", s.CreateUInt32(15));
m.Insert("Hamster", s.CreateString("Trundle"));
```

While this model reduces the idiomatic elegance of C++/WinRT’s projection, it
greatly reduces binary footprint. Instead of each ".Create()" being a fetch of
the property cache, an addref of it, a call, and a releae, it’s a single call
per line with the fetch-and-release applied once in the sequence.

C++/WinRT _does_ cache the real factory interface itself, into a global map of
class-to-factory-interface. Fetching from that cache still requires a lookup, a
lock, a potential factory-cache-miss lookup with call to
`RoGetActivationFactory`, an `AddRef`, then an eventual `Release` of the
interface at the sequence-point.

```c++
// Wasteful
auto v1 = JsonValue::CreateString(...)
auto v2 = JsonValue::CreateObject(...)

// Less wasteful
auto jv = get_activation_factory<winrt::JsonValue, winrt::IJsonValueStatics>();
auto v1 = jv.CreateString(...);
auto v2 = jv.CreateObject(...);
```

This issue can often be avoided by using C++ types within a component boundary,
moving "convert to value set" to the ABI boundary with another component.

## Reduce repeated property fetches

Some objects have a generic property bag available hanging off them, encouraging
an access pattern like (in C++/WinRT):

```c++
foo.Properties().Insert("Puppies", box_value(true));
auto k = unbox_value_or<uint32_t>(foo.Properties().TryLookup("Kittens"), 0);
foo.Properties().Insert("Kittens", box_value(k + 1));
```

Each `.Properties()` is a vtable call, and potentially _also_ a `QueryInterface`
(see above.) Instead, fetch the property set once, then operate on it:

```c++
winrt::IMap<winrt::hstring, winrt::IInspectable> p { foo.Properties() };
p.Insert("Puppies", box_value(true));
auto k = unbox_value_or<uint32_t>(p.TryLookup("Kittens"), 0);
p.Insert("Kittens", box_value(k + 1));
```

Similarly, a common pattern is to repeated check the property value during
computation, especially during iteration of a collection of some kind. Instead,
fetch the property value once and use it. WinRT design guidelines encourage
properties to not "silently change" on instances unless set by the client or
through a method call. Consider this pattern:

```c++
if (IsNoisyAnimal(k.Name()) {
    SetSummary(k.Name() + " are noisy");
} else if (IsCuteAnimal(k.Name())) {
    SetSummary(k.Name() + " are cute");
} else {
    SetSummary(k.Name() + " are something else");
}
```

This causes many calls to IWhateverTypeKIs::Name. A worse example is iteration
on a collection:

```c++
MyType FindAnotherOfMyTypeKind(MyType const& toFind) {
    for (auto const& m : toFind.ChildTypes()) {
        if (m.Kind() == toFind.Kind()) {
          return m;
        }
    }
    return nullptr;
}
```

Each pass through the loop calls `toFind.Kind()`. Instead, save the value off
and use it repeatedly:

```c++
// Ex #1
auto kName = k.Name();
if (IsNoisyAnimal(kName) {
    SetSummary(kName + " are noisy");
} else if (IsCuteAnimal(kName)) {
    SetSummary(kName + " are cute");
} else {
    SetSummary(kName + " are something else");
}

// Ex #2
MyType FindAnotherOfMyTypeKind(MyType const& toFind) {
    auto toFindKind = toFind.Kind();
    for (auto const& m : toFind.ChildTypes()) {
        if (m.Kind() == toFindKind) {
          return m;
        }
    }
    return nullptr;
}
```
