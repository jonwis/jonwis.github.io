#include "pch.h"
#include <string_view>
#include <TraceLoggingProvider.h>
#include "TraceLoggingForStrings.h"

using namespace winrt;
using namespace Windows::Foundation;

/* 264ed6c5-ac31-4454-aec7-ca13afef1bca */

TRACELOGGING_DEFINE_PROVIDER( // defines g_hProvider
    g_hProvider,  // Name of the provider variable
    "MyProvider", // Human-readable name of the provider
    (0x264ed6c5, 0xac31, 0x4454, 0xae, 0xc7, 0xca, 0x13, 0xaf, 0xef, 0x1b, 0xca)); // Provider GUID

int main()
{
    winrt::init_apartment();

    TraceLoggingRegister(g_hProvider);

    std::wstring_view v1{ L"string view 2" };
    std::string_view v2{ "narrow string view 3" };
    winrt::hstring h1{ L"hstring 1" };
    TraceLoggingWrite(g_hProvider, "KittensAndPuppies",
        TraceLoggingValue(v1, "foo"),
        TraceLoggingValue(v2, "zot"),
        TraceLoggingValue(h1, "bar"),
        TraceLoggingStringView(v1),
        TraceLoggingStringView(v2),
        TraceLoggingHString(h1));

    TraceLoggingUnregister(g_hProvider);
}
