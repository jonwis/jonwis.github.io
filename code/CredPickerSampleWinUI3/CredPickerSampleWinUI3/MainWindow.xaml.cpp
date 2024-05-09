#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <winrt/Windows.Security.Credentials.UI.h>
#include <winrt/Windows.Security.Credentials.h>
#include <winrt/Windows.Security.Cryptography.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace winrt::CredPickerSampleWinUI3::implementation
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
        // Show a credential picker to acquire the user's domain credentials.
        Windows::Security::Credentials::UI::CredentialPickerOptions credPickerOptions;
        credPickerOptions.Message(L"Please provide your domain credentials.");
        credPickerOptions.Caption(L"Domain Credentials");
        credPickerOptions.TargetName(L"Contoso");
        credPickerOptions.AuthenticationProtocol(Windows::Security::Credentials::UI::AuthenticationProtocol::Negotiate);
        credPickerOptions.CredentialSaveOption(Windows::Security::Credentials::UI::CredentialSaveOption::Hidden);
        credPickerOptions.CallerSavesCredential(false);

        auto credPickerResults = co_await Windows::Security::Credentials::UI::CredentialPicker::PickAsync(credPickerOptions);

        if (credPickerResults.ErrorCode() == 0)
        {
            // Convert the credential buffer to a base64 string and place that into the button as content
            auto cred = credPickerResults.Credential();
            auto buffer = Windows::Security::Cryptography::CryptographicBuffer::EncodeToBase64String(cred);
            myButton().Content(winrt::box_value(buffer));
        }
        else
        {
            // The user did not provide credentials.
            myButton().Content(winrt::box_value(L"No credentials were provided."));
        }
    }
}
