#include <iostream>
#include <windows.h>

int main(int, char**){
    // Create a temporary directory and two subdirectories "foo" and "bar".
    // Copy the system32 dll XpsDocumentTargetPrint.dll into the temp directory as "test.dll",
    // Copy the system32 dll XpsToTiffConverter.dll into the temp\foo directory as "test.dll".
    // Copy the system32 dll XpsToPwgrConverter.dll into the temp\bar directory as "test.dll".
    // Set the current working directory to the temp directory.
    // Load the tempdir\foo\test.dll by full path, then tempdir\bar\test.dll by full path,
    // then load test.dll by name only (which should resolve to tempdir\test.dll.
    //
    // Print the HMODULE of each one loaded with the path it was loaded from.

    // Create a temp directory in the temp path, whose name is DllPathBinding
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring tempDir = std::wstring(tempPath) + L"DllPathBinding";
    CreateDirectoryW(tempDir.c_str(), NULL);

    // Create the two subdirectories "foo" and "bar"
    std::wstring fooDir = tempDir + L"\\foo";
    std::wstring barDir = tempDir + L"\\bar";
    CreateDirectoryW(fooDir.c_str(), NULL);
    CreateDirectoryW(barDir.c_str(), NULL);

    // Copy the system32 dlls into the respective directories as "test.dll"
    wchar_t system32Path[MAX_PATH];
    GetSystemDirectoryW(system32Path, MAX_PATH);
    CopyFileW((std::wstring(system32Path) + L"\\XpsDocumentTargetPrint.dll").c_str(), (tempDir + L"\\test.dll").c_str(), FALSE);
    CopyFileW((std::wstring(system32Path) + L"\\XpsToTiffConverter.dll").c_str(), (fooDir + L"\\test.dll").c_str(), FALSE);
    CopyFileW((std::wstring(system32Path) + L"\\XpsToPwgrConverter.dll").c_str(), (barDir + L"\\test.dll").c_str(), FALSE);

    // Set the CWD to the temp directory
    SetCurrentDirectoryW(tempDir.c_str());

    // Load the DLLs
    HMODULE hFoo = LoadLibraryW((fooDir + L"\\test.dll").c_str());
    HMODULE hBar = LoadLibraryW((barDir + L"\\test.dll").c_str());
    HMODULE hRoot = LoadLibraryW(L"test.dll");

    // print the pointers as hex values
    std::wcout << L"hFoo:  " << std::hex << hFoo << L" (foo\\test.dll)\n";
    std::wcout << L"hBar:  " << std::hex << hBar << L" (bar\\test.dll)\n";
    std::wcout << L"hRoot: " << std::hex << hRoot << L" (test.dll)\n";

    return 0;
}
