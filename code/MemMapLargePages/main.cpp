#include <iostream>
#include <windows.h>

int main(int, char**){

    // Get the path to the executable, then memory map it using read-only large page support.
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    // Open this file for read access.
    HANDLE hFile = CreateFileW(exePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        std::wcerr << L"Failed to open file: " << exePath << L" Error: " << GetLastError() << std::endl;
        return 1;
    }

    // Create a file mapping object with large page support.
    HANDLE hMap = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (hMap == NULL) {
        std::wcerr << L"Failed to create file mapping. Error: " << GetLastError() << std::endl;
        CloseHandle(hFile);
        return 1;
    }
    
    // Map the view of the file into memory.
    void* pMapView = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (pMapView == NULL) {
        std::wcerr << L"Failed to map view of file. Error: " << GetLastError() << std::endl;
        CloseHandle(hMap);
        CloseHandle(hFile);
        return 1;
    }

    // Determine the page properties of the mapped view
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    std::wcout << L"System Page Size: " << sysInfo.dwPageSize
                << L", Allocation Granularity: " << sysInfo.dwAllocationGranularity << std::endl;
    std::wcout << L"Mapped View Address: " << pMapView << std::endl;
    std::wcout << L"Large Page Size: " << GetLargePageMinimum() << std::endl;

    WIN32_MEMORY_REGION_INFORMATION  mbi;
    if (!QueryVirtualMemoryInformation(GetCurrentProcess(), pMapView, MemoryRegionInfo, &mbi, sizeof(mbi), NULL)) {
        std::wcerr << L"QueryVirtualMemoryInformation failed. Error: " << GetLastError() << std::endl;
    } else {
        std::wcout << L"Region Size: " << mbi.RegionSize << std::endl;
    }

    std::wcout << L"Hello, from MemMapLargePages! Executable Path: " << exePath << std::endl;
}
