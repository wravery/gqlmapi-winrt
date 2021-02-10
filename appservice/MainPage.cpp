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

	Connection connection { true };
	std::int32_t parsedId = -1;
	hstring error;

	if (!co_await connection.ParseQuery(LR"gql(query {
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
		[&parsedId](std::int32_t queryId)
	{
		parsedId = queryId;
	}, [&error](const hstring& message)
	{
		error = message;
	}))
	{
		std::wostringstream oss;

		oss << L"ParseQuery error: " << std::wstring_view { error };
		queryResults().Text(oss.str());
		queryResults().Foreground(SolidColorBrush { Colors::Red() });
		queryResults().Background(SolidColorBrush { Colors::DarkGray() });
		co_return;
	}

	hstring results;

	if (!co_await connection.FetchQuery(parsedId, L"", {},
		[&results](const hstring& fetched)
	{
		results = fetched;
	},
	{},
		[&error](const hstring& message)
	{
		error = message;
	}))
	{
		std::wostringstream oss;

		oss << L"FetchQuery error: " << std::wstring_view { error };
		queryResults().Text(oss.str());
		queryResults().Foreground(SolidColorBrush { Colors::Red() });
		queryResults().Background(SolidColorBrush { Colors::DarkGray() });
		co_return;
	}

	co_await connection.Unsubscribe(parsedId);
	co_await connection.DiscardQuery(parsedId);

	if (!co_await connection.Shutdown([&error](const hstring& message)
	{
		error = message;
	}))
	{
		std::wostringstream oss;

		oss << L"Shutdown error: " << std::wstring_view { error };
		queryResults().Text(oss.str());
		queryResults().Foreground(SolidColorBrush { Colors::Red() });
		queryResults().Background(SolidColorBrush { Colors::DarkGray() });
		co_return;
	}

	queryResults().Text(results);
}

}
