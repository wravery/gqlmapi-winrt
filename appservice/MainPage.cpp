#include "pch.h"
#include "MainPage.h"
#include "MainPage.g.cpp"

#include <sstream>

using namespace winrt;
using namespace clientlib;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::AppService;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Media;

using namespace std::literals;

namespace winrt::appservice::implementation {

MainPage::MainPage()
	: m_connection { true }
{
	InitializeComponent();

	m_resultsForeground = queryResults().Foreground();
	m_resultsBackground = queryResults().Background();
}

fire_and_forget MainPage::ClickHandler(IInspectable const&, RoutedEventArgs const&)
{
	const auto queryText { queryEdit().Text() };

	queryEdit().IsReadOnly(true);
	queryResults().Text(L"Loading...");

	co_await resume_background();

	handle complete { CreateEventW(nullptr, false, false, nullptr) };
	std::int32_t parsedId = -1;
	std::optional<hstring> error;

	co_await m_connection.ParseQuery(queryText,
		[&parsedId, &complete](std::int32_t queryId)
	{
		parsedId = queryId;
		SetEvent(complete.get());
	}, [&error, &complete](const hstring& message)
	{
		error = std::make_optional(message);
		SetEvent(complete.get());
	});

	co_await resume_on_signal(complete.get());

	if (error)
	{
		ShowError(L"ParseQuery"sv, *error);
		co_return;
	}

	hstring results;

	co_await m_connection.FetchQuery(parsedId, L"", {},
		[&results](const JsonObject& fetched)
	{
		results = fetched.ToString();
	},
		[&complete]()
	{
		SetEvent(complete.get());
	},
		[&error, &complete](const hstring& message)
	{
		error = std::make_optional(message);
		SetEvent(complete.get());
	});

	co_await resume_on_signal(complete.get());

	if (error)
	{
		ShowError(L"FetchQuery"sv, *error);
		co_return;
	}

	co_await m_connection.Unsubscribe(parsedId);
	co_await m_connection.DiscardQuery(parsedId);

	co_await resume_foreground(Dispatcher());

	queryResults().Text(results);
	queryResults().Foreground(m_resultsForeground);
	queryResults().Background(m_resultsBackground);

	queryEdit().IsReadOnly(false);
}

fire_and_forget MainPage::PageUnloaded(IInspectable const&, RoutedEventArgs const&)
{
	handle complete { CreateEventW(nullptr, false, false, nullptr) };
	std::optional<hstring> error;

	co_await m_connection.Shutdown(
		[&complete]()
	{
		SetEvent(complete.get());
	},
		[&error, &complete](const hstring& message)
	{
		error = message;
		SetEvent(complete.get());
	});

	co_await resume_on_signal(complete.get());

	if (error)
	{
		ShowError(L"Shutdown"sv, *error);
		co_return;
	}
}

fire_and_forget MainPage::ShowError(std::wstring_view name, std::wstring_view message)
{
	std::wostringstream oss;

	oss << name << L" error: " << message;

	const hstring error { oss.str() };

	co_await resume_foreground(Dispatcher());

	queryResults().Text(error);
	queryResults().Foreground(SolidColorBrush { Colors::Red() });
	queryResults().Background(SolidColorBrush { Colors::DarkGray() });

	queryEdit().IsReadOnly(false);
}

}
