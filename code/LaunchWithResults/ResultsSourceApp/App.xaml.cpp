#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::Windows::AppLifecycle;
namespace windows {
    using namespace Windows::ApplicationModel::Activation;
}

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace winrt::ResultsSourceApp::implementation
{
    /// <summary>
    /// Initializes the singleton application object.  This is the first line of authored code
    /// executed, and as such is the logical equivalent of main() or WinMain().
    /// </summary>
    App::App()
    {
        // Xaml objects should not call InitializeComponent during construction.
        // See https://github.com/microsoft/cppwinrt/tree/master/nuget#initializecomponent

#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
    }

    /// <summary>
    /// Invoked when the application is launched.
    /// </summary>
    /// <param name="e">Details about the launch request and process.</param>
    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        auto activationArgs = AppInstance::GetCurrent().GetActivatedEventArgs();
        
        if (activationArgs.Kind() == ExtendedActivationKind::ProtocolForResults)
        {
            window = make<MainWindow>(activationArgs.Data().as<windows::ProtocolForResultsActivatedEventArgs>());
            window.Activate();
        }
        else if (activationArgs.Kind() == ExtendedActivationKind::Protocol)
        {
            window = make<MainWindow>(activationArgs.Data().as<windows::ProtocolActivatedEventArgs>());
            window.Activate();
        }
        else
        {
            window = make<MainWindow>();
            window.Activate();
        }
    }
}
