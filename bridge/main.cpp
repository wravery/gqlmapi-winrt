#include "pch.h"

#include "MAPIGraphQL.h"
#include "graphqlservice/JSONResponse.h"

#include <windows.h>
#include <DispatcherQueue.h>

#include <atomic>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>

using namespace graphql;

using namespace winrt;
using namespace Windows::ApplicationModel::AppService;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::System;

using namespace std::literals;

struct SubscriptionPayloadQueue : implements<SubscriptionPayloadQueue, Windows::Foundation::IInspectable>
{
	explicit SubscriptionPayloadQueue(const AppServiceConnection& serviceConnection, int requestId) noexcept;
	~SubscriptionPayloadQueue();

	fire_and_forget sendResponse(const JsonObject& responses);
	void Unsubscribe();

	const int requestId;
	bool registered = false;
	std::optional<service::SubscriptionKey> key;
	std::weak_ptr<service::Request> wpService;

	AppServiceConnection serviceConnection;
};

SubscriptionPayloadQueue::SubscriptionPayloadQueue(const AppServiceConnection& serviceConnection, int requestId) noexcept
	: serviceConnection { serviceConnection }
	, requestId { requestId }
{
}

SubscriptionPayloadQueue::~SubscriptionPayloadQueue()
{
	Unsubscribe();
}

fire_and_forget SubscriptionPayloadQueue::sendResponse(const JsonObject& response)
{
	response.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(requestId));

	ValueSet responseMessage;

	responseMessage.Insert(L"responses", PropertyValue::CreateStringArray({
		response.ToString()
		}));

	co_await serviceConnection.SendMessageAsync(responseMessage);
}

void SubscriptionPayloadQueue::Unsubscribe()
{
	if (!registered)
	{
		return;
	}

	registered = false;

	auto deferUnsubscribe = std::move(key);
	auto serviceSingleton = wpService.lock();

	if (deferUnsubscribe
		&& serviceSingleton)
	{
		serviceSingleton->unsubscribe(std::launch::deferred, *deferUnsubscribe).get();
	}
}

class Service : public implements<Service, Windows::Foundation::IInspectable>
{
public:
	explicit Service(const DispatcherQueueController& controller);

	fire_and_forget run();

private:
	void startService(const JsonObject& request);
	void stopService(JsonObject& response);
	void parseQuery(const JsonObject& request, JsonObject& response);
	void discardQuery(const JsonObject& request);
	IAsyncAction fetchQuery(int requestId, const JsonObject& request);
	void unsubscribe(const JsonObject& request);

	IAsyncAction onRequestReceived(const AppServiceConnection& sender, const AppServiceRequestReceivedEventArgs& args);
	void onServiceClosed(const AppServiceConnection& sender, const AppServiceClosedEventArgs& reason);

	IAsyncAction sendResponse(int requestId, const JsonObject& response);
	static JsonObject convertFetchedPayload(std::wstring_view type, std::future<response::Value>&& payload);
	static std::string ConvertToUTF8(std::wstring_view value);
	static std::wstring ConvertToUTF16(std::string_view value);

	std::shared_ptr<service::Request> serviceSingleton;

	DispatcherQueue dispatcherQueue;
	handle shutdownEvent;
	AppServiceConnection serviceConnection;

	std::map<int, peg::ast> queryMap;
	std::map<int, com_ptr<SubscriptionPayloadQueue>> subscriptionMap;
};

Service::Service(const DispatcherQueueController& controller)
	: dispatcherQueue { controller.DispatcherQueue() }
{
	serviceConnection.AppServiceName(L"gqlmapi.bridge");
	serviceConnection.PackageFamilyName(L"a7012456-f540-4a9d-8203-e902b637742f_rs2j33705jmqp");
}

IAsyncAction Service::sendResponse(int requestId, const JsonObject& response)
{
	response.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(requestId));

	ValueSet responseMessage;

	responseMessage.Insert(L"responses", PropertyValue::CreateStringArray({
		response.ToString(),
		}));

	co_await serviceConnection.SendMessageAsync(responseMessage);
}

JsonObject Service::convertFetchedPayload(std::wstring_view type, std::future<response::Value>&& payload)
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

	JsonObject fetched;

	fetched.SetNamedValue(L"type", JsonValue::CreateStringValue(type));
	fetched.SetNamedValue(L"fetched", JsonObject::Parse(ConvertToUTF16(response::toJSON(std::move(document)))));

	return fetched;
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

fire_and_forget Service::run()
{
	co_await resume_background();

	auto status = co_await serviceConnection.OpenAsync();

	if (status != AppServiceConnectionStatus::Success)
	{
		std::ostringstream oss;

		oss << "AppServiceConnection::OpenAsync failed: " << static_cast<int>(status);
		throw std::runtime_error(oss.str());
	}

	shutdownEvent.attach(CreateEventW(nullptr, true, false, nullptr));

	serviceConnection.RequestReceived({ get_weak(), &Service::onRequestReceived });
	serviceConnection.ServiceClosed({ get_weak(), &Service::onServiceClosed });

	co_await resume_on_signal(shutdownEvent.get());

	serviceConnection.Close();

	co_await resume_foreground(dispatcherQueue);

	PostQuitMessage(0);
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
	const auto strong_this { get_strong() };
	const auto queryId { static_cast<int>(request.GetNamedNumber(L"queryId")) };
	const auto itrQuery { queryMap.find(queryId) };

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
	auto payloadQueue = make_self<SubscriptionPayloadQueue>(serviceConnection, requestId);
	auto validationErrors = serviceSingleton->validate(ast);

	if (!validationErrors.empty())
	{
		std::promise<response::Value> promise;

		promise.set_exception(std::make_exception_ptr(service::schema_exception { std::move(validationErrors) }));
		payloadQueue->sendResponse(convertFetchedPayload(L"complete"sv, promise.get_future()));

		co_return;
	}

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
			[weak_queue { payloadQueue->get_weak() }](std::future<response::Value> payload) noexcept -> void
		{
			const auto subscriptionQueue { weak_queue.get() };

			if (!subscriptionQueue)
			{
				return;
			}

			subscriptionQueue->sendResponse(convertFetchedPayload(L"next"sv, std::move(payload)));
		}).get());
	}
	else
	{
		auto payload = serviceSingleton->resolve(std::launch::deferred,
			nullptr,
			ast,
			operationName,
			std::move(parsedVariables));

		payloadQueue->sendResponse(convertFetchedPayload(L"complete"sv, std::move(payload)));
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

IAsyncAction Service::onRequestReceived(const AppServiceConnection& /* sender */, const AppServiceRequestReceivedEventArgs& args)
{
	const auto strong_this { get_strong() };
	const auto messageDeferral { args.GetDeferral() };
	const auto messageRequest { args.Request() };
	const auto message { messageRequest.Message() };
	com_array<hstring> requests;
	bool stopped = false;

	message.Lookup(L"requests").as<IPropertyValue>().GetStringArray(requests);

	co_await resume_foreground(dispatcherQueue);

	for (const auto& request : requests)
	{
		const auto requestObject = JsonObject::Parse(request);
		const auto requestId = static_cast<int>(requestObject.GetNamedNumber(L"requestId"));
		const auto type = requestObject.GetNamedString(L"type");
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
		catch (const hresult_error& hr)
		{
			response = std::make_optional<JsonObject>();
			response->SetNamedValue(L"type", JsonValue::CreateStringValue(L"error"));
			response->SetNamedValue(L"message", JsonValue::CreateStringValue(hr.message()));
		}

		if (response)
		{
			co_await sendResponse(requestId, *response);
		}
	}

	co_await resume_background();

	messageDeferral.Complete();

	if (stopped)
	{
		SetEvent(shutdownEvent.get());
	}
}

void Service::onServiceClosed(const AppServiceConnection& /* sender */, const AppServiceClosedEventArgs& /* reason */)
{
	SetEvent(shutdownEvent.get());
}

int WINAPI wWinMain([[maybe_unused]] HINSTANCE hInstance, [[maybe_unused]] HINSTANCE hPrevInstance, [[maybe_unused]] PWSTR pCmdLine, [[maybe_unused]] int nCmdShow)
{
	init_apartment();

	DispatcherQueueController controller { nullptr };
	DispatcherQueueOptions options {
		sizeof(options),
		DQTYPE_THREAD_CURRENT,
		DQTAT_COM_STA
	};

	check_hresult(CreateDispatcherQueueController(options,
		reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(
			put_abi(controller))));

	try
	{
		Service service { controller };

		service.run();

		MSG message;

		while (GetMessageW(&message, nullptr, 0, 0))
		{
			DispatchMessageW(&message);
		}

		return static_cast<int>(message.wParam);
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
		return 1;
	}
}
