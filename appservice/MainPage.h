#pragma once

#include "MainPage.g.h"

#include <string_view>

namespace winrt::appservice::implementation
{
    struct MainPage : MainPageT<MainPage>
    {
        MainPage();

        fire_and_forget ClickHandler(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::RoutedEventArgs const& args);
        fire_and_forget PageUnloaded(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::RoutedEventArgs const& args);

    private:
        fire_and_forget ShowError(std::wstring_view name, std::wstring_view message);

        clientlib::Connection m_connection;
        Windows::UI::Xaml::Media::Brush m_resultsForeground { nullptr };
        Windows::UI::Xaml::Media::Brush m_resultsBackground { nullptr };
    public:
        void Page_Unloaded(winrt::Windows::Foundation::IInspectable const& sender, winrt::Windows::UI::Xaml::RoutedEventArgs const& e);
    };
}

namespace winrt::appservice::factory_implementation
{
    struct MainPage : MainPageT<MainPage, implementation::MainPage>
    {
    };
}
