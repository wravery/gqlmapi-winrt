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

	m_variablesForeground = queryResults().Foreground();
	m_variablesBackground = queryResults().Background();
}

fire_and_forget MainPage::ClickHandler(IInspectable const&, RoutedEventArgs const&)
{
	const auto queryText { queryEdit().Text() };
	const auto variablesText { variablesEdit().Text() };

	if (variablesText.empty()
		|| JsonObject::TryParse(variablesText, m_variables))
	{
		variablesEdit().Foreground(m_variablesForeground);
		variablesEdit().Background(m_variablesBackground);
	}
	else
	{
		variablesEdit().Foreground(SolidColorBrush { Colors::Red() });
		variablesEdit().Background(SolidColorBrush { Colors::DarkGray() });
	}

	if (m_parsedId)
	{
		co_await UnsubscribeAsync();
	}

	if (m_subscribed)
	{
		runButton().Content(box_value(L"Run"));
		m_subscribed = false;
		co_return;
	}

	queryEdit().IsReadOnly(true);
	queryResults().Text(L"Loading...");

	co_await resume_background();

	co_await m_connection.ParseQuery(queryText,
		[weak_this = get_weak()](std::int32_t queryId) -> IAsyncAction
	{
		if (auto strong_this { weak_this.get() })
		{
			co_await resume_foreground(strong_this->Dispatcher());

			strong_this->OnParsedAsync(queryId);
		}
	}, [weak_this = get_weak()](const hstring& message) -> IAsyncAction
	{
		if (auto strong_this { weak_this.get() })
		{
			co_await resume_foreground(strong_this->Dispatcher());

			strong_this->ShowErrorAsync(L"ParseQuery"sv, message);
		}
	});
}

fire_and_forget MainPage::PageUnloaded(IInspectable const&, RoutedEventArgs const&)
{
	handle complete { CreateEventW(nullptr, false, false, nullptr) };
	std::optional<hstring> error;

	co_await m_connection.Shutdown(
		[&complete]() -> IAsyncAction
	{
		SetEvent(complete.get());
		co_return;
	},
		[&error, &complete](const hstring& message) -> IAsyncAction
	{
		error = message;
		SetEvent(complete.get());
		co_return;
	});

	co_await resume_on_signal(complete.get());

	if (error)
	{
		ShowErrorAsync(L"Shutdown"sv, *error);
		co_return;
	}
}

fire_and_forget MainPage::OnParsedAsync(std::int32_t parsedId)
{
	const auto variables = m_variables;

	m_variables = {};

	co_await UnsubscribeAsync();

	m_parsedId = std::make_optional(parsedId);

	co_await m_connection.FetchQuery(parsedId, L"", variables,
		[weak_this = get_weak()](const JsonObject& fetched) -> IAsyncAction
	{
		if (auto strong_this { weak_this.get() })
		{
			co_await resume_foreground(strong_this->Dispatcher());

			strong_this->OnNextAsync(fetched);
		}
	},
		[weak_this = get_weak()](const JsonObject& fetched) -> IAsyncAction
	{
		if (auto strong_this { weak_this.get() })
		{
			co_await resume_foreground(strong_this->Dispatcher());

			strong_this->OnCompleteAsync(fetched);
		}
	},
		[weak_this = get_weak()](const hstring& message) -> IAsyncAction
	{
		if (auto strong_this { weak_this.get() })
		{
			co_await resume_foreground(strong_this->Dispatcher());

			strong_this->ShowErrorAsync(L"FetchQuery"sv, message);
		}
	});
}

fire_and_forget MainPage::OnNextAsync(const JsonObject& payload)
{
	const auto results { payload.ToString() };

	co_await resume_foreground(Dispatcher());

	queryResults().Text(results);
	queryResults().Foreground(m_resultsForeground);
	queryResults().Background(m_resultsBackground);

	runButton().Content(box_value(L"Unsubscribe"));

	m_subscribed = true;
}

fire_and_forget MainPage::OnCompleteAsync(const JsonObject& payload)
{
	const auto results { payload.ToString() };

	co_await UnsubscribeAsync();

	queryResults().Text(results);
	queryResults().Foreground(m_resultsForeground);
	queryResults().Background(m_resultsBackground);

	queryEdit().IsReadOnly(false);
}

fire_and_forget MainPage::ShowErrorAsync(std::wstring_view name, std::wstring_view message)
{
	std::wostringstream oss;

	oss << name << L" error: " << message;

	const hstring error { oss.str() };

	co_await UnsubscribeAsync();

	queryResults().Text(error);
	queryResults().Foreground(SolidColorBrush { Colors::Red() });
	queryResults().Background(SolidColorBrush { Colors::DarkGray() });

	queryEdit().IsReadOnly(false);
}

Windows::Foundation::IAsyncAction MainPage::UnsubscribeAsync()
{
	co_await resume_foreground(Dispatcher());

	if (m_parsedId)
	{
		const auto previousId = *m_parsedId;

		m_parsedId = std::nullopt;

		co_await resume_background();

		co_await m_connection.Unsubscribe(previousId);
		co_await m_connection.DiscardQuery(previousId);

		co_await resume_foreground(Dispatcher());
	}
}

}
