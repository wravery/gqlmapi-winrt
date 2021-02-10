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
	m_serviceConnection.PackageFamilyName(L"a7012456-f540-4a9d-8203-e902b637742f_jm6713a6qaa9e");
}

IAsyncOperation<bool> Connection::OpenAsync(const ErrorHandler& onError) const
{
	ErrorHandler onErrorCopy { onError };

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

		co_await FullTrustProcessLauncher::LaunchFullTrustProcessForCurrentAppAsync();

		m_opened = true;
	}

	if (!m_started)
	{
		JsonObject startService;

		startService.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(m_nextRequestId++));
		startService.SetNamedValue(L"type", JsonValue::CreateStringValue(L"startService"));
		startService.SetNamedValue(L"useDefaultProfile", JsonValue::CreateBooleanValue(true));

		ValueSet queueRequests;

		queueRequests.Insert(L"command", PropertyValue::CreateString(L"queue-requests"));
		queueRequests.Insert(L"requests", PropertyValue::CreateStringArray({
			startService.ToString(),
			}));

		const auto messageResult = co_await m_serviceConnection.SendMessageAsync(queueRequests);
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

IAsyncOperation<bool> Connection::CloseAsync(const ErrorHandler& onError) const
{
	ErrorHandler onErrorCopy { onError };

	if (m_started)
	{
		const auto requestId = m_nextRequestId++;
		JsonObject stopService;

		stopService.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(requestId));
		stopService.SetNamedValue(L"type", JsonValue::CreateStringValue(L"stopService"));

		ValueSet queueRequests;

		queueRequests.Insert(L"command", PropertyValue::CreateString(L"queue-requests"));
		queueRequests.Insert(L"requests", PropertyValue::CreateStringArray({
			stopService.ToString(),
			}));


		auto messageResult = co_await m_serviceConnection.SendMessageAsync(queueRequests);
		auto messageStatus = messageResult.Status();

		if (messageStatus != AppServiceResponseStatus::Success)
		{
			if (onErrorCopy)
			{
				std::wostringstream oss;

				oss << L"AppServiceConnection::SendMessageAsync(stopService) failed: " << static_cast<int>(messageStatus);
				onErrorCopy(oss.str());
			}

			co_return false;
		}

		ValueSet getResponses;

		getResponses.Insert(L"command", PropertyValue::CreateString(L"get-responses"));

		messageResult = co_await m_serviceConnection.SendMessageAsync(getResponses);
		messageStatus = messageResult.Status();

		if (messageStatus != AppServiceResponseStatus::Success)
		{
			if (onErrorCopy)
			{
				std::wostringstream oss;

				oss << L"AppServiceConnection::SendMessageAsync(getResponses) failed: " << static_cast<int>(messageStatus);
				onErrorCopy(oss.str());
			}

			co_return false;
		}

		com_array<hstring> responses;
		
		messageResult.Message().Lookup(L"responses").as<IPropertyValue>().GetStringArray(responses);

		if (responses.size() != 1)
		{
			if (onErrorCopy)
			{
				std::wostringstream oss;

				oss << L"Unexpected number of responses: " << responses.size();
				onErrorCopy(oss.str());
			}

			co_return false;
		}

		const auto responseObject = JsonObject::Parse(responses.front());
		const auto responseRequestId = static_cast<std::int32_t>(responseObject.GetNamedNumber(L"requestId"));
		const auto responseType = responseObject.GetNamedString(L"type");

		if (responseRequestId != requestId
			|| responseType != L"stopped")
		{
			if (onErrorCopy)
			{
				std::wostringstream oss;

				oss << L"Unexpected requestId: " << responseRequestId << L" type: " << static_cast<std::wstring_view>(responseType);
				onErrorCopy(oss.str());
			}

			co_return false;
		}

		m_started = false;
	}

	if (m_opened)
	{
		m_serviceConnection.Close();
		m_opened = false;
	}

	co_return true;
}

Connection::~Connection()
{
	CloseAsync({}).get();
}

IAsyncOperation<bool> Connection::Shutdown(const ErrorHandler& onError) const
{
	return CloseAsync(onError);
}

IAsyncOperation<bool> Connection::ParseQuery(const hstring& query,
	const ParsedHandler& onParsed, const ErrorHandler& onError) const
{
	hstring queryCopy { query };
	ParsedHandler onParsedCopy { onParsed };
	ErrorHandler onErrorCopy { onError };

	if (!co_await OpenAsync(onError))
	{
		co_return false;
	}

	const auto requestId = m_nextRequestId++;
	JsonObject parseQuery;

	parseQuery.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(requestId));
	parseQuery.SetNamedValue(L"type", JsonValue::CreateStringValue(L"parseQuery"));
	parseQuery.SetNamedValue(L"query", JsonValue::CreateStringValue(queryCopy));

	ValueSet queueRequests;

	queueRequests.Insert(L"command", PropertyValue::CreateString(L"queue-requests"));
	queueRequests.Insert(L"requests", PropertyValue::CreateStringArray({
		parseQuery.ToString(),
		}));

	auto messageResult = co_await m_serviceConnection.SendMessageAsync(queueRequests);
	auto messageStatus = messageResult.Status();

	if (messageStatus != AppServiceResponseStatus::Success)
	{
		if (onErrorCopy)
		{
			std::wostringstream oss;

			oss << L"AppServiceConnection::SendMessageAsync(parseQuery) failed: " << static_cast<int>(messageStatus);
			onErrorCopy(oss.str());
		}

		co_return false;
	}

	ValueSet getResponses;

	getResponses.Insert(L"command", PropertyValue::CreateString(L"get-responses"));

	messageResult = co_await m_serviceConnection.SendMessageAsync(getResponses);
	messageStatus = messageResult.Status();

	if (messageStatus != AppServiceResponseStatus::Success)
	{
		if (onErrorCopy)
		{
			std::wostringstream oss;

			oss << L"AppServiceConnection::SendMessageAsync(getResponses) failed: " << static_cast<int>(messageStatus);
			onErrorCopy(oss.str());
		}

		co_return false;
	}

	com_array<hstring> responses;

	messageResult.Message().Lookup(L"responses").as<IPropertyValue>().GetStringArray(responses);

	if (responses.size() != 1)
	{
		if (onErrorCopy)
		{
			std::wostringstream oss;

			oss << L"Unexpected number of responses: " << responses.size();
			onErrorCopy(oss.str());
		}

		co_return false;
	}

	const auto responseObject = JsonObject::Parse(responses.front());
	const auto responseRequestId = static_cast<std::int32_t>(responseObject.GetNamedNumber(L"requestId"));
	const auto responseType = responseObject.GetNamedString(L"type");

	if (responseRequestId != requestId
		|| responseType != L"parsed")
	{
		if (onErrorCopy)
		{
			std::wostringstream oss;

			oss << L"Unexpected requestId: " << responseRequestId << L" type: " << static_cast<std::wstring_view>(responseType);
			onErrorCopy(oss.str());
		}

		co_return false;
	}

	const int parsedId = static_cast<int>(responseObject.GetNamedNumber(L"queryId"));

	onParsedCopy(parsedId);

	co_return true;
}

IAsyncOperation<bool> Connection::DiscardQuery(std::int32_t queryId) const
{
	if (!m_started)
	{
		co_return false;
	}

	const auto requestId = m_nextRequestId++;
	JsonObject discardQuery;

	discardQuery.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(requestId));
	discardQuery.SetNamedValue(L"type", JsonValue::CreateStringValue(L"discardQuery"));
	discardQuery.SetNamedValue(L"queryId", JsonValue::CreateNumberValue(queryId));

	ValueSet queueRequests;

	queueRequests.Insert(L"command", PropertyValue::CreateString(L"queue-requests"));
	queueRequests.Insert(L"requests", PropertyValue::CreateStringArray({
		discardQuery.ToString(),
		}));

	const auto messageResult = co_await m_serviceConnection.SendMessageAsync(queueRequests);
	const auto messageStatus = messageResult.Status();

	co_return (messageStatus == AppServiceResponseStatus::Success);
}

IAsyncOperation<bool> Connection::FetchQuery(std::int32_t queryId, const hstring& operationName, const JsonObject& variables,
	const FetchedHandler& onFetched, const CompleteHandler& onComplete, const ErrorHandler& onError) const
{
	hstring operationNameCopy { operationName };
	JsonObject variablesCopy { variables };
	FetchedHandler onFetchedCopy { onFetched };
	CompleteHandler onCompleteCopy { onComplete };
	ErrorHandler onErrorCopy { onError };

	if (!co_await OpenAsync(onError))
	{
		co_return false;
	}

	const auto requestId = m_nextRequestId++;
	JsonObject fetchQuery;

	fetchQuery.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(requestId));
	fetchQuery.SetNamedValue(L"type", JsonValue::CreateStringValue(L"fetchQuery"));
	fetchQuery.SetNamedValue(L"queryId", JsonValue::CreateNumberValue(queryId));
	fetchQuery.SetNamedValue(L"operationName", JsonValue::CreateStringValue(operationNameCopy));
	fetchQuery.SetNamedValue(L"variables", variablesCopy);

	ValueSet queueRequests;

	queueRequests.Insert(L"command", PropertyValue::CreateString(L"queue-requests"));
	queueRequests.Insert(L"requests", PropertyValue::CreateStringArray({
		fetchQuery.ToString(),
		}));

	auto messageResult = co_await m_serviceConnection.SendMessageAsync(queueRequests);
	auto messageStatus = messageResult.Status();

	if (messageStatus != AppServiceResponseStatus::Success)
	{
		if (onErrorCopy)
		{
			std::wostringstream oss;

			oss << L"AppServiceConnection::SendMessageAsync(fetchQuery) failed: " << static_cast<int>(messageStatus);
			onErrorCopy(oss.str());
		}

		co_return false;
	}

	ValueSet getResponses;

	getResponses.Insert(L"command", PropertyValue::CreateString(L"get-responses"));

	bool complete = false;

	while (!complete)
	{
		messageResult = co_await m_serviceConnection.SendMessageAsync(getResponses);
		messageStatus = messageResult.Status();

		if (messageStatus != AppServiceResponseStatus::Success)
		{
			if (onErrorCopy)
			{
				std::wostringstream oss;

				oss << L"AppServiceConnection::SendMessageAsync(getResponses) failed: " << static_cast<int>(messageStatus);
				onErrorCopy(oss.str());
			}

			co_return false;
		}

		com_array<hstring> responses;

		messageResult.Message().Lookup(L"responses").as<IPropertyValue>().GetStringArray(responses);

		for (const auto& response : responses)
		{
			const auto responseObject = JsonObject::Parse(response);
			const auto responseRequestId = static_cast<std::int32_t>(responseObject.GetNamedNumber(L"requestId"));
			const auto responseType = responseObject.GetNamedString(L"type");

			if (responseRequestId != requestId)
			{
				if (onErrorCopy)
				{
					std::wostringstream oss;

					oss << L"Unexpected requestId: " << requestId;
					onErrorCopy(oss.str());
				}

				co_return false;
			}

			if (responseType == L"next")
			{
				onFetchedCopy(responseObject.GetNamedObject(L"data").ToString());
			}
			else if (responseType == L"complete")
			{
				if (onCompleteCopy)
				{
					onCompleteCopy();
				}

				complete = true;
			}
			else
			{
				if (onErrorCopy)
				{
					std::wostringstream oss;

					oss << L"Unexpected requestId: " << responseRequestId << L" type: " << static_cast<std::wstring_view>(responseType);
					onErrorCopy(oss.str());
				}

				co_return false;
			}
		}
	}

	co_return true;
}

IAsyncOperation<bool> Connection::Unsubscribe(std::int32_t queryId) const
{
	if (!m_started)
	{
		co_return false;
	}

	const auto requestId = m_nextRequestId++;
	JsonObject unsubscribe;

	unsubscribe.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(requestId));
	unsubscribe.SetNamedValue(L"type", JsonValue::CreateStringValue(L"unsubscribe"));
	unsubscribe.SetNamedValue(L"queryId", JsonValue::CreateNumberValue(queryId));

	ValueSet queueRequests;

	queueRequests.Insert(L"command", PropertyValue::CreateString(L"queue-requests"));
	queueRequests.Insert(L"requests", PropertyValue::CreateStringArray({
		unsubscribe.ToString(),
		}));

	const auto messageResult = co_await m_serviceConnection.SendMessageAsync(queueRequests);
	const auto messageStatus = messageResult.Status();

	co_return (messageStatus == AppServiceResponseStatus::Success);
}

}
