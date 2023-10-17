# Strong Typing is Always Preferable

While it’s attractive to use a "property bag" of typed values to avoid WinRT IDL
definition & implementation costs, the end-to-end cost of defining a
property-bag data-schema-contract is much higher than just providing strongly
typed things.

Consider a type "Data point" with the following properties:

-   Kind – the subtype of a data point
-   Source – where the point came from

And then two derived data types:

-   CarDataPoint
    -   Wheels – an array of "wheel" objects
    -   Displacement – the size of the engine
    -   Color – as it suggests
-   BoatDataPoint
    -   Displacement – the dry weight of the boat, in kilograms
    -   Seats – the number of seats on the boat
    -   Color – as it suggests

A strongly-typed definition of this system might look like:

```c#
runtimeclass DataPoint {
    DataPointKind Kind;
    Guid Source;
}

runtimeclass CarDataPoint : DataPoint {
    CarDataPoint();
    CarWheel[] GetWheels();
    Single Displacement;
    Color Color;
}
runtimeclass BoatDataPoint : DataPoint {
    BoatDataPoint();
    UInt32 Displacement;
    UInt32 Seats;
    Color Color;
}
```

Clients of this object might write this:

```c++
auto const k = p.Kind();
if (k == DataPointKind::Car) {
    auto c = p.as<CarDataPoint>();
    for (auto const& w : c.GetWheels()) {
        // ...
    }
    if (c.Displacement() > 2.0) {
        // very large engine
    }
} else if (k == DataPointKind::Boat) {
    auto b = p.as<BoatDataPoint>();
    if (c.Displacement() > 6000) {
        // very large boat
    } else if (c.Seats() < 3) {
        // canoe?
    }
}
```

A weakly typed definition might instead look like this:

```c#
runtimeclass DataPoint {
    DataPointKind Kind;
    ValueSet Properties;
}
```

The weakly-typed system does not define separate types for Car and Boat. It’s
just expected that if the Kind is Boat, you know what properties are available
to you. So it looks like this:

```c++
auto const k = p.Kind();
auto const props = p.Properties();
if (k == DataPointKind::Car) {
    for (auto const& wv : winrt::unbox_value<winrt::com_array<uint32_t>>(props.Lookup("Wheels"))) {
        auto w = static_cast<CarWheel>(wv); // cast to the enum type
        // use w
    }
    if (props.Lookup("Displacement").as<float>() > 2.0) {
        // very large engine
    }
} else if (k == DataPointKind::Boat) {
    if (props.Lookup("Displacement").as<uint32_t>() > 6000) {
        // very large boat
    } else if (props.Lookup("Seats").as<uint32_t>() < 3) {
        // canoe?
    }
}
```

Where did the list of property names come from? How did the programmer know the
properties exist, and types they can be cast to? Usually what happens is that
some other file gets checked in, like this:

```c++
constexpr static const BoatDataPointDisplacement = L"Displacement"sv; // uint32_t
constexpr static const BoatDataPointSeats = L"Seats"sv; // uint32_t
constexpr static const BoatDataPointColor = L"Color"sv; // uint64_t; RGBA as 64-bit
constexpr static const CarDataPointWheels = L"Wheels"sv; // Array of Wheel, as uint32_t[]
constexpr static const CarDataPointDisplacement = L"Displacement"sv; // float
constexpr static const CarDataPointColor = L"Color"sv; // uint64_t; RBGA as 64-bit
```

... which puts the type information in yet a third place. Not in the IDL, not in
the implementation.

The usual argument is that defining property bags is "easier" earlier in the
design cycle. No need to specify the name in the IDL, or choose a type! Adding a
new property is as easy as `ValueSet::Insert(...,
PropertyValue::CreateInt32(...))`! It’s faster to compile as well, since adding
a new property is just adding a string; changing the type just means changing
two places (the add & retrieval steps.)

The downstream cost of that argument is huge.

-   Mismatches are not detected until test execution
-   Runtime cost of insert/retrieval (and associated memory) is very high
-   Custom marshaling code to convert between value & property set types

## A note on data contracts

Your language and runtime might only support weak typing. Or your serialization
system might only support "store in property bag." This section is specifically
about the "how you code against that model" topic. For serialization I suggest
ProtoBuf (.proto & .protoc). If you must use a weakly typed language, consider
TypeScript.
