﻿#include "pch.h"
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
{
	InitializeComponent();
}

int32_t MainPage::MyProperty()
{
	throw hresult_not_implemented();
}

void MainPage::MyProperty(int32_t /* value */)
{
	throw hresult_not_implemented();
}

fire_and_forget MainPage::ClickHandler(IInspectable const&, RoutedEventArgs const&)
{
	myButton().Content(box_value(L"Clicked"));
	queryResults().Text(L"Loading...");

	co_await resume_background();

	Connection connection { true };
	handle complete { CreateEventW(nullptr, false, false, nullptr) };
	std::int32_t parsedId = -1;
	std::optional<hstring> error;

	co_await connection.ParseQuery(LR"gql(query {
		__schema {
			queryType {
				name
			}
			mutationType {
				name
			}
			subscriptionType {
				name
			}
			types {
				kind
				name
			}
		}
	})gql",
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

	co_await connection.FetchQuery(parsedId, L"", {},
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

	co_await connection.Unsubscribe(parsedId);
	co_await connection.DiscardQuery(parsedId);

	co_await connection.Shutdown(
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

	co_await resume_foreground(Dispatcher());

	queryResults().Text(results);
}

fire_and_forget MainPage::ShowError(std::wstring_view name, std::wstring_view message)
{
	std::wostringstream oss;

	oss << name <<  L" error: " << message;

	const hstring error { oss.str() };

	co_await resume_foreground(Dispatcher());

	queryResults().Text(error);
	queryResults().Foreground(SolidColorBrush { Colors::Red() });
	queryResults().Background(SolidColorBrush { Colors::DarkGray() });
}

}
