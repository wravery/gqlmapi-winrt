#pragma once

#include "Connection.g.h"

#include <atomic>
#include <cstdint>

namespace winrt::clientlib::implementation {

struct Connection : ConnectionT<Connection>
{
	Connection(bool useDefaultProfile);
	~Connection() override;

	Windows::Foundation::IAsyncOperation<bool> Shutdown(const ErrorHandler& onError) const;

	Windows::Foundation::IAsyncOperation<bool> ParseQuery(const hstring& query, const ParsedHandler& onParsed, const ErrorHandler& onError) const;
	Windows::Foundation::IAsyncOperation<bool> DiscardQuery(std::int32_t queryId) const;

	Windows::Foundation::IAsyncOperation<bool> FetchQuery(std::int32_t queryId, const hstring& operationName, const Windows::Data::Json::JsonObject& variables,
		const FetchedHandler& onFetched, const CompleteHandler& onComplete, const ErrorHandler& onError) const;
	Windows::Foundation::IAsyncOperation<bool> Unsubscribe(std::int32_t queryId) const;

private:
	Windows::Foundation::IAsyncOperation<bool> OpenAsync(const ErrorHandler& onError) const;
	Windows::Foundation::IAsyncOperation<bool> CloseAsync(const ErrorHandler& onError) const;

	const bool m_useDefaultProfile;

	mutable bool m_opened = false;
	mutable bool m_started = false;
	mutable std::atomic<std::int32_t> m_nextRequestId;

	Windows::ApplicationModel::AppService::AppServiceConnection m_serviceConnection;
};

}

namespace winrt::clientlib::factory_implementation {

struct Connection : ConnectionT<Connection, implementation::Connection>
{
};

}
