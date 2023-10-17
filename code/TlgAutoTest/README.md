# Adding new TraceLoggingWrite support

TraceLogging is a powerful diagnostics feature available in Windows built on top of ETW.
It supports a variety of primitive types through its generic TraceLoggingValue() method.
This helper adds support for `std::basic_string_view`, `std::basic_string`, and `winrt::hstring`
as tracelogging values.

## Getting started

See [the sample code](./main.cpp) for how to use it. The `TraceLoggingValue`
placeholder macro now accepts views, strings, and hstrings in the usual way:

```c++
#include <TraceLoggingForStrings.h>

void MyFunctionName(std::wstring_view chunkText, winrt::FooType t)
{
    winrt::hstring n = t.Name();

    TraceLoggingWrite(g_hProvider, "FunctionNameCall",
        TraceLoggingValue(chunkText),
        TraceLoggingValue(n, "Foo::Name"));
}
```

You can just copy the header into your project for now.

See also https://github.com/microsoft/tracelogging/issues/60 or maybe https://github.com/microsoft/wil as the final resting places for this support.