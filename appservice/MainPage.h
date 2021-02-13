#pragma once

#include "MainPage.g.h"

#include <optional>
#include <string_view>

namespace winrt::appservice::implementation
{
    struct MainPage : MainPageT<MainPage>
    {
        MainPage();

        fire_and_forget ClickHandler(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::RoutedEventArgs const& args);
        fire_and_forget PageUnloaded(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::RoutedEventArgs const& args);

    private:
        fire_and_forget OnParsedAsync(std::int32_t parsedId);
        fire_and_forget OnNextAsync(const Windows::Data::Json::JsonObject& payload);
        fire_and_forget OnCompleteAsync(const Windows::Data::Json::JsonObject& payload);
        fire_and_forget ShowErrorAsync(std::wstring_view name, std::wstring_view message);
        Windows::Foundation::IAsyncAction UnsubscribeAsync();

        clientlib::Connection m_connection;
        bool m_subscribed = false;
        std::optional<std::int32_t> m_parsedId;
        std::wstring m_operationName;
        Windows::Data::Json::JsonObject m_variables;

        Windows::UI::Xaml::Media::Brush m_resultsForeground { nullptr };
        Windows::UI::Xaml::Media::Brush m_resultsBackground { nullptr };
    };
}

namespace winrt::appservice::factory_implementation
{
    struct MainPage : MainPageT<MainPage, implementation::MainPage>
    {
    };
}
