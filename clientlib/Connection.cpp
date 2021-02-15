#include "pch.h"
#include "Connection.h"
#include "Connection.g.cpp"

#include <stdexcept>
#include <sstream>

using namespace winrt;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::AppService;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;

namespace winrt::clientlib::implementation {

Connection::Connection(bool useDefaultProfile)
	: m_useDefaultProfile { useDefaultProfile }
{
	m_serviceConnection.AppServiceName(L"gqlmapi.client");
	m_serviceConnection.PackageFamilyName(L"a7012456-f540-4a9d-8203-e902b637742f_rs2j33705jmqp");
}

IAsyncOperation<bool> Connection::OpenAsync(const ErrorHandler& onError) const
{
	const auto onErrorCopy { onError };

	if (!m_opened)
	{
		const auto status = co_await m_serviceConnection.OpenAsync();

		if (status != AppServiceConnectionStatus::Success)
		{
			if (onErrorCopy)
			{
				std::wostringstream oss;

				oss << L"AppServiceConnection::OpenAsync failed: " << static_cast<int>(status);
				onErrorCopy(oss.str());
			}

			co_return false;
		}

		m_serviceConnection.RequestReceived({ this, &Connection::OnRequestReceived });

		m_opened = true;
	}

	if (!m_started)
	{
		JsonObject startService;

		startService.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(m_nextRequestId++));
		startService.SetNamedValue(L"type", JsonValue::CreateStringValue(L"startService"));
		startService.SetNamedValue(L"useDefaultProfile", JsonValue::CreateBooleanValue(true));

		ValueSet requests;

		requests.Insert(L"requests", PropertyValue::CreateStringArray({
			startService.ToString(),
			}));

		const auto messageResult = co_await m_serviceConnection.SendMessageAsync(requests);
		const auto messageStatus = messageResult.Status();

		if (messageStatus != AppServiceResponseStatus::Success)
		{
			if (onErrorCopy)
			{
				std::wostringstream oss;

				oss << L"AppServiceConnection::SendMessageAsync(startService) failed: " << static_cast<int>(messageStatus);
				onErrorCopy(oss.str());
			}

			co_return false;
		}

		m_started = true;
	}

	co_return true;
}

void Connection::Close() const
{
	if (m_opened)
	{
		m_serviceConnection.Close();
		m_opened = false;
	}
}

IAsyncAction Connection::OnRequestReceived(const AppServiceConnection& /* sender */, const AppServiceRequestReceivedEventArgs& args) const
{
	const auto strong_this { const_cast<Connection*>(this)->get_strong() };
	const auto messageDeferral { args.GetDeferral() };
	const auto messageRequest { args.Request() };
	const auto message { messageRequest.Message() };
	com_array<hstring> responses;
	bool stopped = false;

	message.Lookup(L"responses").as<IPropertyValue>().GetStringArray(responses);

	for (const auto& response : responses)
	{
		const auto responseObject = JsonObject::Parse(response);
		const auto requestId = static_cast<std::int32_t>(responseObject.GetNamedNumber(L"requestId"));
		const auto type = responseObject.GetNamedString(L"type");

		if (type == L"parsed")
		{
			const auto itr = m_onParsed.find(requestId);

			if (itr != m_onParsed.end())
			{
				co_await itr->second(static_cast<std::int32_t>(responseObject.GetNamedNumber(L"queryId")));
				m_onParsed.erase(itr);
			}

			m_onError.erase(requestId);
		}
		else if (type == L"next")
		{
			const auto itr = m_onNext.find(requestId);

			if (itr != m_onNext.end())
			{
				co_await itr->second(responseObject.GetNamedObject(L"fetched"));
			}
		}
		else if (type == L"complete")
		{
			const auto itr = m_onComplete.find(requestId);

			if (itr != m_onComplete.end())
			{
				co_await itr->second(responseObject.GetNamedObject(L"fetched"));
				m_onComplete.erase(itr);
			}

			m_onNext.erase(requestId);
			m_onError.erase(requestId);
		}
		else if (type == L"error")
		{
			const auto itr = m_onError.find(requestId);

			if (itr != m_onError.end())
			{
				co_await itr->second(responseObject.GetNamedString(L"message"));
				m_onError.erase(itr);
			}

			m_onParsed.erase(requestId);
			m_onNext.erase(requestId);
			m_onComplete.erase(requestId);
			m_onStopped.erase(requestId);
		}
		else if (type == L"stopped")
		{
			const auto itr = m_onStopped.find(requestId);

			if (itr != m_onStopped.end())
			{
				co_await itr->second();
				m_onStopped.erase(itr);
			}

			m_onError.erase(requestId);
			stopped = true;
			break;
		}
		else
		{
			const auto itr = m_onError.find(requestId);

			if (itr != m_onError.end())
			{
				std::wostringstream oss;

				oss << L"Unexpected response type: " << std::wstring_view { type };
				co_await itr->second(oss.str());
			}
		}
	}

	messageDeferral.Complete();

	if (stopped)
	{
		Close();
	}

	co_return;
}

Connection::~Connection()
{
	Close();
}

IAsyncAction Connection::Shutdown(const StoppedHandler& onStopped, const ErrorHandler& onError) const
{
	const auto onStoppedCopy { onStopped };
	const auto onErrorCopy { onError };

	if (m_started)
	{
		const auto requestId = m_nextRequestId++;

		m_onStopped[requestId] = onStoppedCopy;

		if (onErrorCopy)
		{
			m_onError[requestId] = onErrorCopy;
		}

		JsonObject stopService;

		stopService.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(requestId));
		stopService.SetNamedValue(L"type", JsonValue::CreateStringValue(L"stopService"));

		ValueSet requests;

		requests.Insert(L"requests", PropertyValue::CreateStringArray({
			stopService.ToString(),
			}));

		const auto messageResult = co_await m_serviceConnection.SendMessageAsync(requests);

		if (onErrorCopy)
		{
			const auto messageStatus = messageResult.Status();

			if (messageStatus != AppServiceResponseStatus::Success)
			{
				std::wostringstream oss;

				oss << L"AppServiceConnection::SendMessageAsync(stopService) failed: " << static_cast<int>(messageStatus);
				onErrorCopy(oss.str());
			}

			co_return;
		}

		m_started = false;
	}
	else if (onStoppedCopy)
	{
		onStoppedCopy();
	}
}

IAsyncAction Connection::ParseQuery(const hstring& query,
	const ParsedHandler& onParsed, const ErrorHandler& onError) const
{
	const auto queryCopy { query };
	const auto onParsedCopy { onParsed };
	const auto onErrorCopy { onError };

	if (!co_await OpenAsync(onError))
	{
		co_return;
	}

	const auto requestId = m_nextRequestId++;

	m_onParsed[requestId] = onParsedCopy;

	if (onErrorCopy)
	{
		m_onError[requestId] = onErrorCopy;
	}

	JsonObject parseQuery;

	parseQuery.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(requestId));
	parseQuery.SetNamedValue(L"type", JsonValue::CreateStringValue(L"parseQuery"));
	parseQuery.SetNamedValue(L"query", JsonValue::CreateStringValue(queryCopy));

	ValueSet requests;

	requests.Insert(L"requests", PropertyValue::CreateStringArray({
		parseQuery.ToString(),
		}));

	const auto messageResult = co_await m_serviceConnection.SendMessageAsync(requests);

	if (onErrorCopy)
	{
		const auto messageStatus = messageResult.Status();

		if (messageStatus != AppServiceResponseStatus::Success)
		{
			std::wostringstream oss;

			oss << L"AppServiceConnection::SendMessageAsync(parseQuery) failed: " << static_cast<int>(messageStatus);
			onErrorCopy(oss.str());
		}
	}
}

IAsyncAction Connection::DiscardQuery(std::int32_t queryId) const
{
	if (!m_started)
	{
		co_return;
	}

	const auto requestId = m_nextRequestId++;
	JsonObject discardQuery;

	discardQuery.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(requestId));
	discardQuery.SetNamedValue(L"type", JsonValue::CreateStringValue(L"discardQuery"));
	discardQuery.SetNamedValue(L"queryId", JsonValue::CreateNumberValue(queryId));

	ValueSet queueRequests;

	queueRequests.Insert(L"requests", PropertyValue::CreateStringArray({
		discardQuery.ToString(),
		}));

	co_await m_serviceConnection.SendMessageAsync(queueRequests);
}

IAsyncAction Connection::FetchQuery(std::int32_t queryId, const hstring& operationName, const JsonObject& variables,
	const FetchedHandler& onNext, const FetchedHandler& onComplete, const ErrorHandler& onError) const
{
	const auto operationNameCopy { operationName };
	const auto variablesCopy { variables };
	const auto onNextCopy { onNext };
	const auto onCompleteCopy { onComplete };
	const auto onErrorCopy { onError };

	if (!co_await OpenAsync(onError))
	{
		co_return;
	}

	const auto requestId = m_nextRequestId++;

	if (onNextCopy)
	{
		m_onNext[requestId] = onNextCopy;
	}

	if (onCompleteCopy)
	{
		m_onComplete[requestId] = onCompleteCopy;
	}

	if (onErrorCopy)
	{
		m_onError[requestId] = onErrorCopy;
	}

	JsonObject fetchQuery;

	fetchQuery.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(requestId));
	fetchQuery.SetNamedValue(L"type", JsonValue::CreateStringValue(L"fetchQuery"));
	fetchQuery.SetNamedValue(L"queryId", JsonValue::CreateNumberValue(queryId));
	fetchQuery.SetNamedValue(L"operationName", JsonValue::CreateStringValue(operationNameCopy));
	fetchQuery.SetNamedValue(L"variables", variablesCopy);

	ValueSet queueRequests;

	queueRequests.Insert(L"requests", PropertyValue::CreateStringArray({
		fetchQuery.ToString(),
		}));

	const auto messageResult = co_await m_serviceConnection.SendMessageAsync(queueRequests);

	if (onErrorCopy)
	{
		const auto messageStatus = messageResult.Status();

		if (messageStatus != AppServiceResponseStatus::Success)
		{
			std::wostringstream oss;

			oss << L"AppServiceConnection::SendMessageAsync(fetchQuery) failed: " << static_cast<int>(messageStatus);
			onErrorCopy(oss.str());
		}
	}
}

IAsyncAction Connection::Unsubscribe(std::int32_t queryId) const
{
	if (!m_started)
	{
		co_return;
	}

	const auto requestId = m_nextRequestId++;
	JsonObject unsubscribe;

	unsubscribe.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(requestId));
	unsubscribe.SetNamedValue(L"type", JsonValue::CreateStringValue(L"unsubscribe"));
	unsubscribe.SetNamedValue(L"queryId", JsonValue::CreateNumberValue(queryId));

	ValueSet queueRequests;

	queueRequests.Insert(L"requests", PropertyValue::CreateStringArray({
		unsubscribe.ToString(),
		}));

	co_await m_serviceConnection.SendMessageAsync(queueRequests);
}

}
