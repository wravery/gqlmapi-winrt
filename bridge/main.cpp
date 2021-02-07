#include "pch.h"

#include "MAPIGraphQL.h"
#include "graphqlservice/JSONResponse.h"

#include <windows.h>

#include <array>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>

using namespace graphql;

using namespace winrt;
using namespace Windows::ApplicationModel::AppService;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;

using namespace std::literals;

class Service : public std::enable_shared_from_this<Service>
{
public:
	explicit Service();
	~Service();

	void run();

private:
	void startService(const JsonObject& request);
	void stopService();
	void parseQuery(const JsonObject& request, JsonObject& response);
	void discardQuery(const JsonObject& request);
	void fetchQuery(int requestId, const JsonObject& request, JsonObject& response);
	void unsubscribe(const JsonObject& request);

	void sendResponse(int requestId, JsonObject& response);
	static JsonObject getPayloadData(std::future<response::Value>&& payload);
	static std::string ConvertToUTF8(std::wstring_view value);
	static std::wstring ConvertToUTF16(std::string_view value);

	std::shared_ptr<service::Request> serviceSingleton;

	std::mutex writeMutex;
	AppServiceConnection serviceConnection;

	std::map<int, peg::ast> queryMap;
	std::map<int, service::SubscriptionKey> subscriptionMap;
};

Service::Service()
{
	serviceConnection.AppServiceName(L"gqlmapi");
	serviceConnection.PackageFamilyName(L"a7012456-f540-4a9d-8203-e902b637742f_jm6713a6qaa9e");

	auto status = serviceConnection.OpenAsync().get();

	if (status != AppServiceConnectionStatus::Success)
	{
		std::ostringstream oss;

		oss << "AppServiceConnection::OpenAsync failed: " << static_cast<int>(status);
		throw std::runtime_error(oss.str());
	}
}

Service::~Service()
{
	serviceConnection.Close();
}

void Service::sendResponse(int requestId, JsonObject& response)
{
	std::lock_guard lock { writeMutex };

	response.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(requestId));

	ValueSet responseMessage;

	responseMessage.Insert(L"command", PropertyValue::CreateString(L"send-responses"));
	responseMessage.Insert(L"responses", PropertyValue::CreateStringArray({
		response.ToString(),
		}));

	serviceConnection.SendMessageAsync(responseMessage).get();
}

std::string Service::ConvertToUTF8(std::wstring_view value)
{
	std::string result;

	if (!value.empty())
	{
		const auto cch = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);

		if (cch != 0)
		{
			result.resize(static_cast<size_t>(cch));

			if (cch != WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), cch, nullptr, nullptr))
			{
				result.clear();
			}
		}
	}

	return result;
}

std::wstring Service::ConvertToUTF16(std::string_view value)
{
	std::wstring result;

	if (!value.empty())
	{
		const auto cch = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);

		if (cch != 0)
		{
			result.resize(static_cast<size_t>(cch));

			if (cch != MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), cch))
			{
				result.clear();
			}
		}
	}

	return result;
}

void Service::run()
{
	ValueSet getRequests;

	getRequests.Insert(L"command", PropertyValue::CreateString(L"get-requests"));

	auto serviceResponse = serviceConnection.SendMessageAsync(getRequests).get();

	while (serviceResponse.Status() == AppServiceResponseStatus::Success)
	{
		bool stopped = false;
		com_array<hstring> requests;

		serviceResponse.Message().Lookup(L"requests").as<IPropertyValue>().GetStringArray(requests);

		for (const auto& request : requests)
		{
			const auto requestObject = JsonObject::Parse(request);
			auto requestId = static_cast<int>(requestObject.GetNamedNumber(L"requestId"));
			auto type = requestObject.GetNamedString(L"type");
			std::optional<JsonObject> response;

			try
			{
				if (type == L"startService")
				{
					startService(requestObject);
				}
				else if (type == L"stopService")
				{
					stopService();
					stopped = true;
					break;
				}
				else if (type == L"parseQuery")
				{
					response = std::make_optional<JsonObject>();
					parseQuery(requestObject, *response);
				}
				else if (type == L"discardQuery")
				{
					discardQuery(requestObject);
				}
				else if (type == L"fetchQuery")
				{
					response = std::make_optional<JsonObject>();
					fetchQuery(requestId, requestObject, *response);
				}
				else if (type == L"unsubscribe")
				{
					unsubscribe(requestObject);
				}
				else
				{
					std::ostringstream oss;

					oss << "Unknown request type: " << ConvertToUTF8(type);
					throw std::logic_error { oss.str() };
				}
			}
			catch (const std::exception& ex)
			{
				response = std::make_optional<JsonObject>();
				response->SetNamedValue(L"type", JsonValue::CreateStringValue(L"error"));
				response->SetNamedValue(L"message", JsonValue::CreateStringValue(ConvertToUTF16(ex.what())));
			}

			if (response)
			{
				sendResponse(requestId, *response);
			}
		}

		if (stopped)
		{
			break;
		}

		serviceResponse = serviceConnection.SendMessageAsync(getRequests).get();
	}
}

void Service::startService(const JsonObject& request)
{
	serviceSingleton = mapi::GetService(request.GetNamedBoolean(L"useDefaultProfile"));
}

void Service::stopService()
{
	if (serviceSingleton)
	{
		for (const auto& entry : subscriptionMap)
		{
			serviceSingleton->unsubscribe(std::launch::deferred, entry.second).get();
		}

		subscriptionMap.clear();
		queryMap.clear();
		serviceSingleton.reset();
	}
}

void Service::parseQuery(const JsonObject& request, JsonObject& response)
{
	const int queryId = (queryMap.empty() ? 1 : queryMap.crbegin()->first + 1);

	queryMap[queryId] = peg::parseString(ConvertToUTF8(request.GetNamedString(L"query")));

	response.SetNamedValue(L"type", JsonValue::CreateStringValue(L"parsed"));
	response.SetNamedValue(L"queryId", JsonValue::CreateNumberValue(queryId));
}

void Service::discardQuery(const JsonObject& request)
{
	queryMap.erase(static_cast<int>(request.GetNamedNumber(L"queryId")));
}

JsonObject Service::getPayloadData(std::future<response::Value>&& payload)
{
	response::Value document { response::Type::Map };

	try
	{
		document = payload.get();
	}
	catch (service::schema_exception& scx)
	{
		document.reserve(2);
		document.emplace_back(std::string { service::strData }, {});
		document.emplace_back(std::string { service::strErrors }, scx.getErrors());
	}
	catch (const std::exception& ex)
	{
		std::ostringstream oss;

		oss << "Caught exception delivering subscription payload: " << ex.what();
		document.reserve(2);
		document.emplace_back(std::string { service::strData }, {});
		document.emplace_back(std::string { service::strErrors }, response::Value { oss.str() });
	}

	return JsonObject::Parse(ConvertToUTF16(response::toJSON(std::move(document))));
}

void Service::fetchQuery(int requestId, const JsonObject& request, JsonObject& response)
{
	const auto queryId = static_cast<int>(request.GetNamedNumber(L"queryId"));
	const auto itrQuery = queryMap.find(queryId);

	if (itrQuery == queryMap.cend())
	{
		throw std::runtime_error("Unknown queryId");
	}

	auto& ast = itrQuery->second;
	constexpr auto operationNameKey = L"operationName"sv;
	auto operationName = request.HasKey(operationNameKey)
		? ConvertToUTF8(request.GetNamedString(operationNameKey))
		: ""s;
	constexpr auto variablesKey = L"variables"sv;
	auto parsedVariables = (request.HasKey(variablesKey)
		? response::parseJSON(ConvertToUTF8(request.GetNamedObject(variablesKey).ToString()))
		: response::Value(response::Type::Map));

	if (serviceSingleton->findOperationDefinition(ast, operationName).first
		== service::strSubscription)
	{
		if (subscriptionMap.find(queryId) != subscriptionMap.end())
		{
			throw std::runtime_error("Duplicate subscription");
		}

		subscriptionMap[queryId] =
			serviceSingleton
			->subscribe(std::launch::deferred,
				service::SubscriptionParams { nullptr,
					peg::ast { ast },
					std::move(operationName),
					std::move(parsedVariables) },
				[wpThis = std::weak_ptr<Service> { shared_from_this() },
				requestId](std::future<response::Value> payload) noexcept -> void {
			auto spThis = wpThis.lock();

			if (!spThis)
			{
				return;
			}

			JsonObject next;

			next.SetNamedValue(L"type", JsonValue::CreateStringValue(L"next"));
			next.SetNamedValue(L"data", getPayloadData(std::move(payload)));

			spThis->sendResponse(requestId, next);
		}).get();
	}
	else
	{
		response.SetNamedValue(L"type", JsonValue::CreateStringValue(L"complete"));
		response.SetNamedValue(L"data",
			getPayloadData(serviceSingleton->resolve(std::launch::deferred,
				nullptr,
				ast,
				operationName,
				std::move(parsedVariables))));
	}
}

void Service::unsubscribe(const JsonObject& request)
{
	auto itr = subscriptionMap.find(static_cast<int>(request.GetNamedNumber(L"queryId")));

	if (itr != subscriptionMap.end())
	{
		if (serviceSingleton)
		{
			serviceSingleton->unsubscribe(std::launch::deferred, itr->second).get();
		}

		subscriptionMap.erase(itr);
	}
}

int WINAPI wWinMain([[maybe_unused]] HINSTANCE hInstance, [[maybe_unused]] HINSTANCE hPrevInstance, [[maybe_unused]] PWSTR pCmdLine, [[maybe_unused]] int nCmdShow)
{
	init_apartment();

	try
	{
		std::make_shared<Service>()->run();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
		return 1;
	}

	return 0;
}
