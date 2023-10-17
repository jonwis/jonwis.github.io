# Adding new TraceLoggingWrite support

TraceLogging is a powerful diagnostics feature available in Windows built on top of ETW.
It supports a variety of primitive types through its generic TraceLoggingValue() method.
This helper adds support for `std::string_view`, `std::wstring_view`, and `winrt::hstring`
for formatting strings.

## Getting started

See [the sample code](./main.cpp) for how to trace string views. The `TraceLoggingValue`
placeholder macro now accepts views and hstrings in the usual way.

```c++
#include <TraceLoggingForStrings.h>

void MyFunctionName(std::wstring_view chunkText, winrt::FooType t)
{
    winrt::hstring n = t.Name();

    TraceLoggingWrite(g_hProvider, "FunctioNameCall",
        TraceLoggingValue(chunkText),
        TraceLoggingValue(n, "Foo::Name"));
}
```
