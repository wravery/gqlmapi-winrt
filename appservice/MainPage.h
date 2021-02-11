#pragma once

#include "MainPage.g.h"

#include <string_view>

namespace winrt::appservice::implementation
{
    struct MainPage : MainPageT<MainPage>
    {
        MainPage();

        int32_t MyProperty();
        void MyProperty(int32_t value);

        fire_and_forget ClickHandler(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::RoutedEventArgs const& args);

    private:
        fire_and_forget ShowError(std::wstring_view name, std::wstring_view message);
    };
}

namespace winrt::appservice::factory_implementation
{
    struct MainPage : MainPageT<MainPage, implementation::MainPage>
    {
    };
}
