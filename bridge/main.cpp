#include "pch.h"

#include "MAPIGraphQL.h"
#include "graphqlservice/JSONResponse.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <iostream>
#include <map>
#include <memory>
#include <queue>
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

struct SubscriptionPayloadQueue : std::enable_shared_from_this<SubscriptionPayloadQueue>
{
	explicit SubscriptionPayloadQueue() noexcept;
	~SubscriptionPayloadQueue();

	void sendResponse(JsonObject& response);
	void Unsubscribe();

	int requestId = 0;
	bool registered = false;
	std::mutex mutex;
	std::unique_ptr<std::thread> worker;
	std::condition_variable condition;
	std::queue<std::future<response::Value>> payloads;
	std::optional<service::SubscriptionKey> key;
	std::weak_ptr<service::Request> wpService;

	AppServiceConnection serviceConnection;
};

SubscriptionPayloadQueue::SubscriptionPayloadQueue() noexcept
{
	serviceConnection.AppServiceName(L"gqlmapi");
	serviceConnection.PackageFamilyName(L"a7012456-f540-4a9d-8203-e902b637742f_jm6713a6qaa9e");
}

SubscriptionPayloadQueue::~SubscriptionPayloadQueue()
{
	Unsubscribe();
}

void SubscriptionPayloadQueue::sendResponse(JsonObject& response)
{
	response.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(requestId));

	ValueSet responseMessage;

	responseMessage.Insert(L"command", PropertyValue::CreateString(L"send-responses"));
	responseMessage.Insert(L"responses", PropertyValue::CreateStringArray({
		response.ToString(),
		}));

	serviceConnection.SendMessageAsync(responseMessage).get();
}

void SubscriptionPayloadQueue::Unsubscribe()
{
	std::unique_lock<std::mutex> lock(mutex);

	if (worker)
	{
		worker->join();
		worker.reset();
	}

	if (!registered)
	{
		return;
	}

	registered = false;

	auto deferUnsubscribe = std::move(key);
	auto serviceSingleton = wpService.lock();

	lock.unlock();
	condition.notify_one();

	if (deferUnsubscribe
		&& serviceSingleton)
	{
		serviceSingleton->unsubscribe(std::launch::deferred, *deferUnsubscribe).get();
	}
}

class Service : public implements<Service, IInspectable>
{
public:
	explicit Service();
	~Service();

	IAsyncAction run();

private:
	void startService(const JsonObject& request);
	void stopService(JsonObject& response);
	void parseQuery(const JsonObject& request, JsonObject& response);
	void discardQuery(const JsonObject& request);
	IAsyncAction fetchQuery(int requestId, const JsonObject& request);
	void unsubscribe(const JsonObject& request);

	IAsyncAction sendResponse(int requestId, JsonObject& response);
	static std::string ConvertToUTF8(std::wstring_view value);
	static std::wstring ConvertToUTF16(std::string_view value);

	std::shared_ptr<service::Request> serviceSingleton;

	std::atomic_bool shutdown;

	AppServiceConnection serviceConnection;

	std::map<int, peg::ast> queryMap;
	std::map<int, std::shared_ptr<SubscriptionPayloadQueue>> subscriptionMap;
};

Service::Service()
{
	serviceConnection.AppServiceName(L"gqlmapi");
	serviceConnection.PackageFamilyName(L"a7012456-f540-4a9d-8203-e902b637742f_jm6713a6qaa9e");
}

Service::~Service()
{
	serviceConnection.Close();
}

IAsyncAction Service::sendResponse(int requestId, JsonObject& response)
{
	response.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(requestId));

	ValueSet responseMessage;

	responseMessage.Insert(L"command", PropertyValue::CreateString(L"send-responses"));
	responseMessage.Insert(L"responses", PropertyValue::CreateStringArray({
		response.ToString(),
		}));

	co_await serviceConnection.SendMessageAsync(responseMessage);
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

IAsyncAction Service::run()
{
	auto status = co_await serviceConnection.OpenAsync();

	if (status != AppServiceConnectionStatus::Success)
	{
		std::ostringstream oss;

		oss << "AppServiceConnection::OpenAsync failed: " << static_cast<int>(status);
		throw std::runtime_error(oss.str());
	}

	ValueSet getRequests;

	getRequests.Insert(L"command", PropertyValue::CreateString(L"get-requests"));

	auto serviceResponse = co_await serviceConnection.SendMessageAsync(getRequests);

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
					response = std::make_optional<JsonObject>();
					stopService(*response);
					stopped = true;
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
					co_await fetchQuery(requestId, requestObject);
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
				co_await sendResponse(requestId, *response);
			}
		}

		if (stopped)
		{
			break;
		}

		serviceResponse = co_await serviceConnection.SendMessageAsync(getRequests);
	}

	co_return;
}

void Service::startService(const JsonObject& request)
{
	serviceSingleton = mapi::GetService(request.GetNamedBoolean(L"useDefaultProfile"));
}

void Service::stopService(JsonObject& response)
{
	if (serviceSingleton)
	{
		for (const auto& entry : subscriptionMap)
		{
			entry.second->Unsubscribe();
		}

		subscriptionMap.clear();
		queryMap.clear();
		serviceSingleton.reset();
	}

	response.SetNamedValue(L"type", JsonValue::CreateStringValue(L"stopped"));
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

IAsyncAction Service::fetchQuery(int requestId, const JsonObject& request)
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

	auto payloadQueue = std::make_shared<SubscriptionPayloadQueue>();

	co_await payloadQueue->serviceConnection.OpenAsync();

	std::unique_lock lock { payloadQueue->mutex };

	payloadQueue->requestId = requestId;
	payloadQueue->worker = std::make_unique<std::thread>([payloadQueue]()
	{
		bool registered = true;

		while (registered)
		{
			std::unique_lock<std::mutex> lock(payloadQueue->mutex);

			payloadQueue->condition.wait(lock, [payloadQueue]() noexcept -> bool
			{
				return !payloadQueue->registered
					|| !payloadQueue->payloads.empty();
			});

			auto payloads = std::move(payloadQueue->payloads);

			registered = payloadQueue->registered;
			lock.unlock();

			std::vector<std::string> json;

			while (!payloads.empty())
			{
				response::Value document { response::Type::Map };
				auto payload = std::move(payloads.front());

				payloads.pop();

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

				json.push_back(response::toJSON(std::move(document)));
			}

			for (const auto& data : json)
			{
				JsonObject next;

				next.SetNamedValue(L"type", JsonValue::CreateStringValue(L"next"));
				next.SetNamedValue(L"data", JsonObject::Parse(ConvertToUTF16(data)));

				payloadQueue->sendResponse(next);
			}
		}

		JsonObject complete;

		complete.SetNamedValue(L"type", JsonValue::CreateStringValue(L"complete"));

		payloadQueue->sendResponse(complete);
		payloadQueue->serviceConnection.Close();
	});

	if (serviceSingleton->findOperationDefinition(ast, operationName).first == service::strSubscription)
	{
		if (subscriptionMap.find(queryId) != subscriptionMap.end())
		{
			throw std::runtime_error("Duplicate subscription");
		}

		payloadQueue->registered = true;
		payloadQueue->key = std::make_optional(serviceSingleton->subscribe(std::launch::deferred,
			service::SubscriptionParams { nullptr,
				peg::ast { ast },
				std::move(operationName),
				std::move(parsedVariables) },
			[payloadQueue](std::future<response::Value> payload) noexcept -> void
		{
			std::unique_lock lock { payloadQueue->mutex };

			if (!payloadQueue->registered)
			{
				return;
			}
			payloadQueue->payloads.push(std::move(payload));

			lock.unlock();
			payloadQueue->condition.notify_one();
		}).get());
	}
	else
	{
		payloadQueue->payloads.push(serviceSingleton->resolve(std::launch::deferred,
			nullptr,
			ast,
			operationName,
			std::move(parsedVariables)));

		lock.unlock();
		payloadQueue->condition.notify_one();
	}

	subscriptionMap[queryId] = std::move(payloadQueue);

	co_return;
}

void Service::unsubscribe(const JsonObject& request)
{
	auto itr = subscriptionMap.find(static_cast<int>(request.GetNamedNumber(L"queryId")));

	if (itr != subscriptionMap.end())
	{
		itr->second->Unsubscribe();
		subscriptionMap.erase(itr);
	}
}

int WINAPI wWinMain([[maybe_unused]] HINSTANCE hInstance, [[maybe_unused]] HINSTANCE hPrevInstance, [[maybe_unused]] PWSTR pCmdLine, [[maybe_unused]] int nCmdShow)
{
	init_apartment();

	try
	{
		Service service;

		service.run().get();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
		return 1;
	}

	return 0;
}
