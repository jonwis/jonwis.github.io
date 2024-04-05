#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Windows::Foundation::Collections;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace winrt::ResultsSourceApp::implementation
{
    void MainWindow::InitializeComponent()
    {
        MainWindowT::InitializeComponent();

        if (m_resultsArgs)
        {
            // Pull out the URI from the activation args, put the query string in the prompt box
            if (auto uri = m_resultsArgs.Uri())
            {
                if (auto uriQuery = uri.QueryParsed())
                {
                    UriPrompt().Text(uriQuery.GetFirstValueByName(L"prompt"));
                }
            }
        }
        else if (m_args)
        {
            // Pull out the URI from the activation args, put the query string in the prompt box
            if (auto uri = m_args.Uri())
            {
                if (auto uriQuery = uri.QueryParsed())
                {
                    UriPrompt().Text(uriQuery.GetFirstValueByName(L"prompt"));
                }
            }

            myButton().Visibility(Visibility::Collapsed);
        }
    }

    int32_t MainWindow::MyProperty()
    {
        return 0;
    }

    void MainWindow::MyProperty(int32_t /* value */)
    {
    }

    void MainWindow::myButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_resultsArgs)
        {
            if (auto result = m_resultsArgs.ProtocolForResultsOperation())
            {
                auto props = ValueSet();
                props.Insert(L"response", box_value(LaunchResponse().Text()));
                result.ReportCompleted(props);
            }
        }
    }
}
