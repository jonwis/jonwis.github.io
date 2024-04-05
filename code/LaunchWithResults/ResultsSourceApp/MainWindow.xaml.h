#pragma once

#include "MainWindow.g.h"

namespace winrt::ResultsSourceApp::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow() = default;

        MainWindow(winrt::Windows::ApplicationModel::Activation::ProtocolForResultsActivatedEventArgs const& args) : m_resultsArgs(args)
        {
        }

        MainWindow(winrt::Windows::ApplicationModel::Activation::ProtocolActivatedEventArgs const& args) : m_args(args)
        {
        }

        void InitializeComponent();
        int32_t MyProperty();
        void MyProperty(int32_t value);

        void myButton_Click(IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::ApplicationModel::Activation::ProtocolForResultsActivatedEventArgs m_resultsArgs{ nullptr };
        winrt::Windows::ApplicationModel::Activation::ProtocolActivatedEventArgs m_args{ nullptr };
    };
}

namespace winrt::ResultsSourceApp::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
