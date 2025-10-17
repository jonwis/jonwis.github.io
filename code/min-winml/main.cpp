#include <iostream>
#include <windows.h>
#include <appmodel.h>
#include <roapi.h>
#include <pathcch.h>
#include <windows.foundation.h>
#include <wininet.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>

// From WinAppSDK
#define ORT_API_MANUAL_INIT 1
#include <Microsoft.Windows.AI.MachineLearning.h>
#include <WindowsAppSDK-VersionInfo.h>
#include <win_onnxruntime_c_api.h>
#include <win_onnxruntime_cxx_api.h>

#include <wil/result_macros.h>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

// This is a complete sample of using Windows App SDK 1.8.126-experimental to load OnnxRuntime using WinML,
// dynamic dependencies, and other functionality.
//
// The WinML headers are pulled in from the WindowsAppSDK submodule (release/1.8.126-experimental), from the
// ABI-WinRT generated headers and MIT licensed OSS for binding to the Onnx Runtime. No WinAppSDK NuGet
// or SDK required.
// 
// In your application, you should pre-install the Windows App SDK runtime using one of the methods listed
// on https://learn.microsoft.com/windows/apps/windows-app-sdk/deploy-packaged-apps ... This demonstration
// downloads the installer executable (if the Runtime is not installed) before running the rest.
//
// The full version is "Windows App SDK 1.8 Experimental4 (1.8.250702007-experimental4)".
// See https://aka.ms/windowsappsdk/1.8/1.8.250702007-experimental4/Microsoft.WindowsAppRuntime.Redist.1.8.zip
// which contains the MSIX packages and runtime installer. Note that installing the packages requires also
// adding the store license.  The installer executable used below handles it all automatically.
//
// This app does some simple probing to see if the installer executable is already present before downloading
// and running it. A production ready example should compare the downloaded thing against the known sha256
// hash of the installer - 07369BBEB1E64404A856458353639C16DB12A18458E579BA6062D43F0BBB94B0.
//
// Remove packages to try this out using some powershell:
// get-appxpackage | ?{ $_.Dependencies -match "WindowsAppRuntime.1.8-experimental4" } |% PackageFullName | Remove-AppxPackage
// get-appxpackage *WindowsAppRuntime.1.8-experimental4* | Remove-AppxPackage
// get-appxpackage *WindowsMLRuntime* | Remove-AppxPackage
//
HRESULT AddWinAppSDKReference(PACKAGEDEPENDENCY_CONTEXT* context)
{
    // Create a dynamic dependency on the WinAppSDK 1.8.126-experimental package
    PACKAGE_VERSION runtimeVersion;
    runtimeVersion.Major = Microsoft::WindowsAppSDK::Runtime::Version::Major;
    runtimeVersion.Minor = Microsoft::WindowsAppSDK::Runtime::Version::Minor;
    runtimeVersion.Build = Microsoft::WindowsAppSDK::Runtime::Version::Build;
    runtimeVersion.Revision = Microsoft::WindowsAppSDK::Runtime::Version::Revision;
    wil::unique_process_heap_string dependencyId;
    auto hr = ::TryCreatePackageDependency(nullptr,
        Microsoft::WindowsAppSDK::Runtime::Packages::Framework::PackageFamilyName,
        runtimeVersion,
        PackageDependencyProcessorArchitectures_X64,
        PackageDependencyLifetimeKind_Process,
        nullptr,
        CreatePackageDependencyOptions_None,
        &dependencyId);

    if (SUCCEEDED(hr)) 
    {
        wil::unique_process_heap_string fullName;
        RETURN_IF_FAILED(::AddPackageDependency(dependencyId.get(), 0, AddPackageDependencyOptions_PrependIfRankCollision, context, &fullName));
    }

    return S_OK;
}

HRESULT AcquireAndInstallRuntime()
{
    // See if it's next to the executable; if not, go download it
    auto exeInstallerName = L"Microsoft.WindowsAppRuntime.Installer.1.8.250702007-experimental4.exe";
    auto exeUri = L"https://aka.ms/windowsappsdk/1.8/1.8.250702007-experimental4/windowsappruntimeinstall-x64.exe";
    WCHAR installerPath[MAX_PATH] = {};
    auto moduleFileName = wil::GetModuleFileNameW<wil::unique_process_heap_string>(nullptr);
    RETURN_IF_FAILED(PathCchCombine(installerPath, ARRAYSIZE(installerPath), moduleFileName.get(), exeInstallerName));

    if (GetFileAttributesW(installerPath) == INVALID_FILE_ATTRIBUTES)
    {
        wil::unique_process_heap_string downloadsPath;
        RETURN_IF_FAILED(wil::ExpandEnvironmentStringsW<wil::unique_process_heap_string>(L"%USERPROFILE%\\Downloads", downloadsPath));
        RETURN_IF_FAILED(PathCchCombine(installerPath, ARRAYSIZE(installerPath), downloadsPath.get(), exeInstallerName));

        // Download the installer to the downloads folder; make sure it's the one we expected.
        ::DeleteFileW(installerPath);
        RETURN_IF_FAILED(URLDownloadToFileW(nullptr, exeUri, installerPath, 0, nullptr));
    }

    wil::unique_process_information processInfo;
    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;
    RETURN_IF_WIN32_BOOL_FALSE(CreateProcessW(
        installerPath,
        nullptr,
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo));
    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode{};
    RETURN_IF_WIN32_BOOL_FALSE(GetExitCodeProcess(processInfo.hProcess, &exitCode));
    RETURN_HR_IF(E_FAIL, exitCode != 0);

    return S_OK;
}

HRESULT GetWinAppSDKReady(PACKAGEDEPENDENCY_CONTEXT* context)
{
    RETURN_IF_FAILED(AddWinAppSDKReference(context));
    if (!*context)
    {
        RETURN_IF_FAILED(AcquireAndInstallRuntime());
        RETURN_IF_FAILED(AddWinAppSDKReference(context));
    }

    return S_OK;
}

HRESULT SetUpWinML()
{
    // Configure COM/WinRT runtime
    RETURN_IF_FAILED(::Windows::Foundation::Initialize(RO_INIT_MULTITHREADED));

    // Configure the Windows App SDK dynamic dependency for the WinML runtime
    PACKAGEDEPENDENCY_CONTEXT packageDependencyContext{};
    RETURN_IF_FAILED(GetWinAppSDKReady(&packageDependencyContext));
    RETURN_HR_IF(E_FAIL, !packageDependencyContext);

    // Get the Onnx Runtime API base pointer
    auto temp = OrtGetApiBase();
    Ort::InitApi();
    Ort::Env env;
    auto devices = env.GetEpDevices();
    printf("Before registration, %zu devices\n", devices.size());

    // Get the WinML factory instance, use it to get a catalog instance
    Microsoft::WRL::ComPtr<ABI::Microsoft::Windows::AI::MachineLearning::IExecutionProviderCatalogStatics> executionProviderCatalogStatics;
    RETURN_IF_FAILED(::Windows::Foundation::GetActivationFactory(
        Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Microsoft_Windows_AI_MachineLearning_ExecutionProviderCatalog).Get(),
        &executionProviderCatalogStatics));

    Microsoft::WRL::ComPtr<ABI::Microsoft::Windows::AI::MachineLearning::IExecutionProviderCatalog> executionProviderCatalog;
    RETURN_IF_FAILED(executionProviderCatalogStatics->GetDefault(&executionProviderCatalog));

    // Register all execution providers asynchronously, wait for them to finish
    Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncOperationWithProgress<ABI::Windows::Foundation::Collections::IVector<ABI::Microsoft::Windows::AI::MachineLearning::ExecutionProvider*>*, double>> registerAllOperation;
    wil::unique_event_nothrow registrationCompletedEvent;
    RETURN_IF_FAILED(executionProviderCatalog->EnsureAndRegisterAllAsync(&registerAllOperation));
    RETURN_IF_FAILED(registrationCompletedEvent.create(wil::EventOptions::ManualReset, nullptr));
    RETURN_IF_FAILED(registerAllOperation->put_Completed(
        Microsoft::WRL::Callback<ABI::Windows::Foundation::IAsyncOperationWithProgressCompletedHandler<ABI::Windows::Foundation::Collections::IVector<ABI::Microsoft::Windows::AI::MachineLearning::ExecutionProvider*>*, double>>(
            [&registrationCompletedEvent](auto operation, auto status) -> HRESULT
            {
                registrationCompletedEvent.SetEvent();
                return S_OK;
            }).Get()));
    RETURN_LAST_ERROR_IF(!registrationCompletedEvent.wait());

    devices = env.GetEpDevices();
    printf("After registration, %zu devices\n", devices.size());

    return S_OK;
}

int main(int, char**){
    HRESULT hr = SetUpWinML();
    if (FAILED(hr)) {
        printf("Failed to set up WinML with error code: 0x%08X\n", hr);
        return -1;
    }
    else {
        printf("Successfully set up WinML with OnnxRuntime!\n");
        return 0;
    }
}
