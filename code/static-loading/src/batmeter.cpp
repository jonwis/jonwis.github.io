#include "batmeter.h"

#include <windows.h>
#include <string>

namespace
{
void PrintHello(HMODULE module)
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));

    const std::wstring message = std::wstring(L"hello from ") + path + L"\r\n";
    HANDLE stdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stdoutHandle != nullptr && stdoutHandle != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteConsoleW(stdoutHandle, message.c_str(), static_cast<DWORD>(message.size()), &written, nullptr);
    }
}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        PrintHello(module);
    }

    return TRUE;
}

extern "C" BATMETER_API void RunBatMeter()
{
}
