#include "pch.h"
#include "MainPage.h"
#include "MainPage.g.cpp"

#include <sstream>

using namespace winrt;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::AppService;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Xaml;

namespace winrt::appservice::implementation
{
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

    IAsyncAction MainPage::ClickHandler(IInspectable const&, RoutedEventArgs const&)
    {
        myButton().Content(box_value(L"Clicked"));

        FullTrustProcessLauncher::LaunchFullTrustProcessForCurrentAppAsync();

        hstring results;

        JsonObject startService;

        startService.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(0));
        startService.SetNamedValue(L"type", JsonValue::CreateStringValue(L"startService"));
        startService.SetNamedValue(L"useDefaultProfile", JsonValue::CreateBooleanValue(true));

        JsonObject parseQuery;

        parseQuery.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(1));
        parseQuery.SetNamedValue(L"type", JsonValue::CreateStringValue(L"parseQuery"));
        parseQuery.SetNamedValue(L"query", JsonValue::CreateStringValue(LR"gql(query {
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
        })gql"));

        AppServiceConnection serviceConnection;

        serviceConnection.AppServiceName(L"gqlmapi");
        serviceConnection.PackageFamilyName(L"a7012456-f540-4a9d-8203-e902b637742f_jm6713a6qaa9e");

        auto status = co_await serviceConnection.OpenAsync();

        if (status != AppServiceConnectionStatus::Success)
        {
            std::ostringstream oss;

            oss << "AppServiceConnection::OpenAsync failed: " << static_cast<int>(status);
            throw std::runtime_error(oss.str());
        }

        ValueSet startAndParse;

        startAndParse.Insert(L"command", PropertyValue::CreateString(L"queue-requests"));
        startAndParse.Insert(L"requests", PropertyValue::CreateStringArray({
            startService.ToString(),
            parseQuery.ToString(),
        }));

        auto messageStatus = (co_await serviceConnection.SendMessageAsync(startAndParse)).Status();

        if (messageStatus != AppServiceResponseStatus::Success)
        {
            std::ostringstream oss;

            oss << "AppServiceConnection::SendMessageAsync(startAndParse) failed: " << static_cast<int>(messageStatus);
            throw std::runtime_error(oss.str());
        }

        ValueSet getResponses;

        getResponses.Insert(L"command", PropertyValue::CreateString(L"get-responses"));

        auto serviceResponses = co_await serviceConnection.SendMessageAsync(getResponses);

        messageStatus = serviceResponses.Status();

        if (messageStatus != AppServiceResponseStatus::Success)
        {
            std::ostringstream oss;

            oss << "AppServiceConnection::SendMessageAsync(getResponses) failed: " << static_cast<int>(messageStatus);
            throw std::runtime_error(oss.str());
        }

        com_array<hstring> responses;

        serviceResponses.Message().Lookup(L"responses").as<IPropertyValue>().GetStringArray(responses);

        if (responses.size() != 1)
        {
            std::ostringstream oss;

            oss << "Unexpected number of responses: " << responses.size();
            throw std::runtime_error(oss.str());
        }

        auto responseObject = JsonObject::Parse(responses.front());
        auto requestId = static_cast<int>(responseObject.GetNamedNumber(L"requestId"));
        auto type = responseObject.GetNamedString(L"type");

        if (requestId != 1 || type != L"parsed")
        {
            std::ostringstream oss;

            oss << "Unexpected requestId: " << requestId;
            throw std::runtime_error(oss.str());
        }

        const int parsedId = static_cast<int>(responseObject.GetNamedNumber(L"queryId"));
        JsonObject fetchQuery;

        fetchQuery.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(2));
        fetchQuery.SetNamedValue(L"type", JsonValue::CreateStringValue(L"fetchQuery"));
        fetchQuery.SetNamedValue(L"queryId", JsonValue::CreateNumberValue(parsedId));

        ValueSet fetch;

        fetch.Insert(L"command", PropertyValue::CreateString(L"queue-requests"));
        fetch.Insert(L"requests", PropertyValue::CreateStringArray({
            fetchQuery.ToString(),
        }));

        serviceResponses = co_await serviceConnection.SendMessageAsync(fetch);
        messageStatus = serviceResponses.Status();

        if (messageStatus != AppServiceResponseStatus::Success)
        {
            std::ostringstream oss;

            oss << "AppServiceConnection::SendMessageAsync(fetch) failed: " << static_cast<int>(messageStatus);
            throw std::runtime_error(oss.str());
        }

        bool complete = false;

        while (!complete)
        {
            serviceResponses = co_await serviceConnection.SendMessageAsync(getResponses);
            messageStatus = serviceResponses.Status();

            if (messageStatus != AppServiceResponseStatus::Success)
            {
                std::ostringstream oss;

                oss << "AppServiceConnection::SendMessageAsync(getResponses) failed: " << static_cast<int>(messageStatus);
                throw std::runtime_error(oss.str());
            }

            serviceResponses.Message().Lookup(L"responses").as<IPropertyValue>().GetStringArray(responses);

            for (const auto& response : responses)
            {
                responseObject = JsonObject::Parse(response);
                requestId = static_cast<int>(responseObject.GetNamedNumber(L"requestId"));
                type = responseObject.GetNamedString(L"type");

                if (requestId != 2)
                {
                    std::ostringstream oss;

                    oss << "Unexpected requestId: " << requestId;
                    throw std::runtime_error(oss.str());
                }

                if (type == L"next")
                {
                    results = responseObject.GetNamedObject(L"data").ToString();
                }
                else if (type == L"complete")
                {
                    complete = true;
                }
            }
        }

        JsonObject unsubscribe;

        unsubscribe.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(3));
        unsubscribe.SetNamedValue(L"type", JsonValue::CreateStringValue(L"unsubscribe"));
        unsubscribe.SetNamedValue(L"queryId", JsonValue::CreateNumberValue(parsedId));

        JsonObject discardQuery;

        discardQuery.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(4));
        discardQuery.SetNamedValue(L"type", JsonValue::CreateStringValue(L"discardQuery"));
        discardQuery.SetNamedValue(L"queryId", JsonValue::CreateNumberValue(parsedId));

        JsonObject stopService;

        stopService.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(5));
        stopService.SetNamedValue(L"type", JsonValue::CreateStringValue(L"stopService"));

        ValueSet shutdown;

        shutdown.Insert(L"command", PropertyValue::CreateString(L"queue-requests"));
        shutdown.Insert(L"requests", PropertyValue::CreateStringArray({
            unsubscribe.ToString(),
            discardQuery.ToString(),
            stopService.ToString(),
        }));

        serviceResponses = co_await serviceConnection.SendMessageAsync(shutdown);
        messageStatus = serviceResponses.Status();

        if (messageStatus != AppServiceResponseStatus::Success)
        {
            std::ostringstream oss;

            oss << "AppServiceConnection::SendMessageAsync(shutdown) failed: " << static_cast<int>(messageStatus);
            throw std::runtime_error(oss.str());
        }

        serviceResponses = co_await serviceConnection.SendMessageAsync(getResponses);
        messageStatus = serviceResponses.Status();

        if (messageStatus != AppServiceResponseStatus::Success)
        {
            std::ostringstream oss;

            oss << "AppServiceConnection::SendMessageAsync(getResponses) failed: " << static_cast<int>(messageStatus);
            throw std::runtime_error(oss.str());
        }

        serviceResponses.Message().Lookup(L"responses").as<IPropertyValue>().GetStringArray(responses);

        if (responses.size() != 1)
        {
            std::ostringstream oss;

            oss << "Unexpected number of responses: " << responses.size();
            throw std::runtime_error(oss.str());
        }

        responseObject = JsonObject::Parse(responses.front());
        requestId = static_cast<int>(responseObject.GetNamedNumber(L"requestId"));
        type = responseObject.GetNamedString(L"type");

        if (requestId != 5 || type != L"stopped")
        {
            std::ostringstream oss;

            oss << "Unexpected requestId: " << requestId;
            throw std::runtime_error(oss.str());
        }
    }
}
