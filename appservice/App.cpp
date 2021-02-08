#include "pch.h"

#include "App.h"
#include "MainPage.h"

#include <algorithm>

using namespace winrt;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::ApplicationModel::AppService;
using namespace Windows::ApplicationModel::Background;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Navigation;
using namespace appservice;
using namespace appservice::implementation;

/// <summary>
/// Initializes the singleton application object.  This is the first line of authored code
/// executed, and as such is the logical equivalent of main() or WinMain().
/// </summary>
App::App()
{
    InitializeComponent();
    Suspending({ this, &App::OnSuspending });

    {
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

        std::lock_guard lock { m_requestQueue.mutex };

        m_requestQueue.requests.push_back(std::move(startService));
        m_requestQueue.requests.push_back(std::move(parseQuery));
    }


#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
    UnhandledException([this](IInspectable const&, UnhandledExceptionEventArgs const& e)
    {
        if (IsDebuggerPresent())
        {
            auto errorMessage = e.Message();
            __debugbreak();
        }
    });
#endif
}

/// <summary>
/// Invoked when the application is launched normally by the end user.  Other entry points
/// will be used such as when the application is launched to open a specific file.
/// </summary>
/// <param name="e">Details about the launch request and process.</param>
void App::OnLaunched(LaunchActivatedEventArgs const& e)
{
    Frame rootFrame{ nullptr };
    auto content = Window::Current().Content();
    if (content)
    {
        rootFrame = content.try_as<Frame>();
    }

    // Do not repeat app initialization when the Window already has content,
    // just ensure that the window is active
    if (rootFrame == nullptr)
    {
        // Create a Frame to act as the navigation context and associate it with
        // a SuspensionManager key
        rootFrame = Frame();

        rootFrame.NavigationFailed({ this, &App::OnNavigationFailed });

        if (e.PreviousExecutionState() == ApplicationExecutionState::Terminated)
        {
            // Restore the saved session state only when appropriate, scheduling the
            // final launch steps after the restore is complete
        }

        if (e.PrelaunchActivated() == false)
        {
            if (rootFrame.Content() == nullptr)
            {
                // When the navigation stack isn't restored navigate to the first page,
                // configuring the new page by passing required information as a navigation
                // parameter
                rootFrame.Navigate(xaml_typename<appservice::MainPage>(), box_value(e.Arguments()));
            }
            // Place the frame in the current Window
            Window::Current().Content(rootFrame);
            // Ensure the current window is active
            Window::Current().Activate();
        }
    }
    else
    {
        if (e.PrelaunchActivated() == false)
        {
            if (rootFrame.Content() == nullptr)
            {
                // When the navigation stack isn't restored navigate to the first page,
                // configuring the new page by passing required information as a navigation
                // parameter
                rootFrame.Navigate(xaml_typename<appservice::MainPage>(), box_value(e.Arguments()));
            }
            // Ensure the current window is active
            Window::Current().Activate();
        }
    }
}

/// <summary>
/// Invoked when application execution is being suspended.  Application state is saved
/// without knowing whether the application will be terminated or resumed with the contents
/// of memory still intact.
/// </summary>
/// <param name="sender">The source of the suspend request.</param>
/// <param name="e">Details about the suspend request.</param>
void App::OnSuspending([[maybe_unused]] IInspectable const& sender, [[maybe_unused]] SuspendingEventArgs const& e)
{
    // Save application state and stop any background activity
}

/// <summary>
/// Invoked when Navigation to a certain page fails
/// </summary>
/// <param name="sender">The Frame which failed navigation</param>
/// <param name="e">Details about the navigation failure</param>
void App::OnNavigationFailed(IInspectable const&, NavigationFailedEventArgs const& e)
{
    throw hresult_error(E_FAIL, hstring(L"Failed to load Page ") + e.SourcePageType().Name);
}

ServiceConnection::ServiceConnection(int connectionId, App& app)
    : m_connectionId { connectionId }
    , m_app { app }
{
}

void ServiceConnection::OnAppServicesCanceled(IBackgroundTaskInstance const& /*sender*/, BackgroundTaskCancellationReason const& /*reason*/)
{
    ShutdownService();
}

void ServiceConnection::OnServiceClosed(AppServiceConnection const& /*sender*/, AppServiceClosedEventArgs const& /*reason*/)
{
    ShutdownService();
}

void ServiceConnection::ShutdownService()
{
    m_backgroundTaskDeferral.Complete();
    m_backgroundTaskDeferral = nullptr;
    m_appServiceConnection = nullptr;

    m_app.OnServiceConnectionShutdown(m_connectionId);
}

void App::OnBackgroundActivated(BackgroundActivatedEventArgs const& e)
{
    auto taskInstance = e.TaskInstance();
    auto appServiceTrigger = taskInstance.TriggerDetails().as<AppServiceTriggerDetails>();
    auto taskDeferral = taskInstance.GetDeferral();

    if (m_requestQueue.shutdown)
    {
        taskDeferral.Complete();
        return;
    }

    const int lastConnectionId = m_serviceConnections.empty() ? 0 : m_serviceConnections.crbegin()->first;
    const int nextConnectionId = lastConnectionId + 1;

    m_serviceConnections[nextConnectionId] = std::make_unique<ServiceConnection>(nextConnectionId, *this);

    auto serviceConnection = m_serviceConnections[nextConnectionId].get();

    serviceConnection->m_backgroundTaskDeferral = taskDeferral;
    taskInstance.Canceled({ serviceConnection, &ServiceConnection::OnAppServicesCanceled });

    serviceConnection->m_appServiceConnection = appServiceTrigger.AppServiceConnection();
    serviceConnection->m_appServiceConnection.RequestReceived({ this, &App::OnRequestReceived });
    serviceConnection->m_appServiceConnection.ServiceClosed({ serviceConnection, &ServiceConnection::OnServiceClosed });

}

void App::OnServiceConnectionShutdown(int connectionId)
{
    m_serviceConnections.erase(connectionId);
}

IAsyncAction App::OnRequestReceived(AppServiceConnection const& /*sender*/, AppServiceRequestReceivedEventArgs const& args)
{
    auto messageDeferral = args.GetDeferral();
    auto messageRequest = args.Request();
    auto message = messageRequest.Message();
    const std::wstring_view command { message.Lookup(L"command").as<hstring>() };

    if (command == L"get-requests")
    {
        apartment_context callingThread;

        co_await resume_background();

        std::unique_lock lock { m_requestQueue.mutex };

        m_requestQueue.condition.wait(lock, [this]() noexcept
        {
            return m_requestQueue.shutdown
                || !m_requestQueue.requests.empty();
        });

        com_array<hstring> requests(static_cast<com_array<hstring>::size_type>(m_requestQueue.requests.size()));

        std::transform(m_requestQueue.requests.begin(), m_requestQueue.requests.end(), requests.begin(),
            [](const JsonObject& request) -> hstring
        {
            return request.ToString();
        });

        m_requestQueue.requests.clear();
        lock.unlock();

        co_await callingThread;

        ValueSet returnValue;

        returnValue.Insert(L"requests", PropertyValue::CreateStringArray(requests));

        co_await messageRequest.SendResponseAsync(returnValue);
    }
    else if (command == L"send-responses")
    {
        com_array<hstring> responses;

        message.Lookup(L"responses").as<IPropertyValue>().GetStringArray(responses);

        for (const auto& response : responses)
        {
            auto responseObject = JsonObject::Parse(response);
            auto requestId = static_cast<int>(responseObject.GetNamedNumber(L"requestId"));
            auto type = responseObject.GetNamedString(L"type");

            if (requestId == 1 && type == L"parsed")
            {
                parsedId = static_cast<int>(responseObject.GetNamedNumber(L"queryId"));

                JsonObject fetchQuery;

                fetchQuery.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(2));
                fetchQuery.SetNamedValue(L"type", JsonValue::CreateStringValue(L"fetchQuery"));
                fetchQuery.SetNamedValue(L"queryId", JsonValue::CreateNumberValue(parsedId));

                std::unique_lock lock { m_requestQueue.mutex };

                m_requestQueue.requests.push_back(std::move(fetchQuery));

                lock.unlock();
                m_requestQueue.condition.notify_one();
            }
            else if (requestId == 2 && type == L"next")
            {
                results = responseObject.GetNamedObject(L"data").ToString();
            }
            else if (requestId == 2 && type == L"complete")
            {
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

                std::unique_lock lock { m_requestQueue.mutex };

                m_requestQueue.requests.push_back(std::move(discardQuery));
                m_requestQueue.requests.push_back(std::move(stopService));

                lock.unlock();
                m_requestQueue.condition.notify_one();
            }
            else if (requestId == 5 && type == L"stopped")
            {
                std::unique_lock lock { m_requestQueue.mutex };

                m_requestQueue.shutdown = true;

                lock.unlock();
                m_requestQueue.condition.notify_one();
            }
        }
    }

    messageDeferral.Complete();
}
