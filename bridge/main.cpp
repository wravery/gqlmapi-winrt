#include "pch.h"

#include "MAPIGraphQL.h"
#include "graphqlservice/JSONResponse.h"

#include <windows.h>

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

using namespace std::literals;

struct SubscriptionPayloadQueue : implements<SubscriptionPayloadQueue, IInspectable>
{
	explicit SubscriptionPayloadQueue(const AppServiceConnection& serviceConnection, int requestId) noexcept;
	~SubscriptionPayloadQueue();

	fire_and_forget sendResponses(const com_array<JsonObject>& responses);
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

fire_and_forget SubscriptionPayloadQueue::sendResponses(const com_array<JsonObject>& responses)
{
	com_array<hstring> payloads(static_cast<com_array<hstring>::size_type>(responses.size()));

	std::transform(responses.begin(), responses.end(), payloads.begin(),
		[id = requestId](const JsonObject& response)
	{
		response.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(id));
		return response.ToString();
	});

	ValueSet responseMessage;

	responseMessage.Insert(L"responses", PropertyValue::CreateStringArray(payloads));

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

	JsonObject complete;

	complete.SetNamedValue(L"type", JsonValue::CreateStringValue(L"complete"));

	sendResponses({ complete });
}

class Service : public implements<Service, IInspectable>
{
public:
	explicit Service();

	IAsyncAction run();

private:
	void startService(const JsonObject& request);
	void stopService(JsonObject& response);
	void parseQuery(const JsonObject& request, JsonObject& response);
	void discardQuery(const JsonObject& request);
	IAsyncAction fetchQuery(int requestId, const JsonObject& request);
	void unsubscribe(const JsonObject& request);

	IAsyncAction onRequestReceived(const AppServiceConnection& sender, const AppServiceRequestReceivedEventArgs& args);

	IAsyncAction sendResponse(int requestId, const JsonObject& response);
	static JsonObject convertNextPayload(std::future<response::Value>&& payload);
	static JsonObject buildComplete();
	static std::string ConvertToUTF8(std::wstring_view value);
	static std::wstring ConvertToUTF16(std::string_view value);

	std::shared_ptr<service::Request> serviceSingleton;

	handle shutdownEvent;
	AppServiceConnection serviceConnection;

	std::map<int, peg::ast> queryMap;
	std::map<int, com_ptr<SubscriptionPayloadQueue>> subscriptionMap;
};

Service::Service()
{
	serviceConnection.AppServiceName(L"gqlmapi.bridge");
	serviceConnection.PackageFamilyName(L"a7012456-f540-4a9d-8203-e902b637742f_jm6713a6qaa9e");
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

JsonObject Service::convertNextPayload(std::future<response::Value>&& payload)
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

	JsonObject next;

	next.SetNamedValue(L"type", JsonValue::CreateStringValue(L"next"));
	next.SetNamedValue(L"fetched", JsonObject::Parse(ConvertToUTF16(response::toJSON(std::move(document)))));

	return next;
}

JsonObject Service::buildComplete()
{
	JsonObject complete;

	complete.SetNamedValue(L"type", JsonValue::CreateStringValue(L"complete"));

	return complete;
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

	co_await resume_on_signal(shutdownEvent.get());

	serviceConnection.Close();
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

	SubscriptionPayloadQueue payloadQueue { serviceConnection, requestId };

	if (serviceSingleton->findOperationDefinition(ast, operationName).first == service::strSubscription)
	{
		if (subscriptionMap.find(queryId) != subscriptionMap.end())
		{
			throw std::runtime_error("Duplicate subscription");
		}

		payloadQueue.registered = true;
		payloadQueue.key = std::make_optional(serviceSingleton->subscribe(std::launch::deferred,
			service::SubscriptionParams { nullptr,
				peg::ast { ast },
				std::move(operationName),
				std::move(parsedVariables) },
			[weak_queue { payloadQueue.get_weak() }](std::future<response::Value> payload) noexcept -> void
		{
			const auto subscriptionQueue { weak_queue.get() };

			if (!subscriptionQueue)
			{
				return;
			}

			subscriptionQueue->sendResponses({
				convertNextPayload(std::move(payload)),
				});
		}).get());
	}
	else
	{
		auto payload = serviceSingleton->resolve(std::launch::deferred,
			nullptr,
			ast,
			operationName,
			std::move(parsedVariables));

		payloadQueue.sendResponses({
			convertNextPayload(std::move(payload)),
			buildComplete(),
			});
	}

	subscriptionMap[queryId] = payloadQueue.get_strong();

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

		if (response)
		{
			co_await sendResponse(requestId, *response);
		}
	}

	messageDeferral.Complete();

	if (stopped)
	{
		SetEvent(shutdownEvent.get());
	}
}

int WINAPI wWinMain([[maybe_unused]] HINSTANCE hInstance, [[maybe_unused]] HINSTANCE hPrevInstance, [[maybe_unused]] PWSTR pCmdLine, [[maybe_unused]] int nCmdShow)
{
	init_apartment();

	//MessageBoxW(HWND_DESKTOP, L"bridge.exe is ready...", L"Attach Debugger", MB_OK);

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
