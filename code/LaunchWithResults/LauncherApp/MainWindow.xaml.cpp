#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include <winrt/Windows.System.h>
#include <format>
#include <ShlObj_core.h>
#include <microsoft.ui.xaml.window.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Windows::System;
using namespace Windows::Foundation;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace winrt::LauncherApp::implementation
{
    int32_t MainWindow::MyProperty()
    {
        throw hresult_not_implemented();
    }

    void MainWindow::MyProperty(int32_t /* value */)
    {
        throw hresult_not_implemented();
    }

    winrt::fire_and_forget MainWindow::myButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto lifetime{ get_strong() };

        myButton().IsEnabled(false);

        // Launching for results from a Win32 app requires passing along our HWND and a target application
        // to launch for results. Use the HWND under this Window, and just pick the first app that implements
        // our preferred protocol.
        auto options = LauncherOptions();
        HWND myWindow;
        winrt::check_hresult(this->m_inner.as<::IWindowNative>()->get_WindowHandle(&myWindow));
        options.as<::IInitializeWithWindow>()->Initialize(myWindow);

        auto handlers = co_await Launcher::FindUriSchemeHandlersAsync(L"x-launch-results");
        if (handlers.Size() > 0)
        {
            options.TargetApplicationPackageFamilyName(handlers.GetAt(0).PackageFamilyName());
        }

        // Get the string content in QueryText and form up a URI to be sent to the launch handler
        // for x-launch-results:, like "x-launch-results:moo?prompt=Hello%20World". Launch that URI
        // for results and then place the result of the launch in the text block.
        auto uri = Uri(L"x-launch-results:moo?prompt=" + QueryText().Text());
        auto result = co_await Launcher::LaunchUriForResultsAsync(uri, options);

        if (result.Status() == LaunchUriStatus::Success)
        {
            std::wstring resultString = L"Success: ";

            for (auto const& [key, value] : result.Result())
            {
                std::format_to(std::back_inserter(resultString), L"[{} : {}],", key, value.as<IStringable>().ToString());
            }

            ResponseContent().Text(resultString);
        }
        else if (result.Status() == LaunchUriStatus::ProtocolUnavailable)
        {
            ResponseContent().Text(L"Protocol unavailable");
        }
        else if (result.Status() == LaunchUriStatus::AppUnavailable)
        {
            ResponseContent().Text(L"App unavailable");
        }
        else if (result.Status() == LaunchUriStatus::Unknown)
        {
            ResponseContent().Text(L"Unknown error");
        }

        myButton().Content(box_value(L"Clicked"));
        myButton().IsEnabled(true);
    }
}
