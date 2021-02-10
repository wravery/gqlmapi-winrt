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

    const auto appServiceName = serviceConnection->m_appServiceConnection.AppServiceName();

    if (appServiceName == L"gqlmapi.client")
    {
        serviceConnection->m_appServiceConnection.RequestReceived({ this, &App::OnClientRequestReceived });
    }
    else if (appServiceName == L"gqlmapi.bridge")
    {
        serviceConnection->m_appServiceConnection.RequestReceived({ this, &App::OnBridgeResponseReceived });
    }
 
    serviceConnection->m_appServiceConnection.ServiceClosed({ serviceConnection, &ServiceConnection::OnServiceClosed });
}

void App::OnServiceConnectionShutdown(int connectionId)
{
    m_serviceConnections.erase(connectionId);
}

IAsyncAction App::OnClientRequestReceived(AppServiceConnection const& /*sender*/, AppServiceRequestReceivedEventArgs const& args)
{
    auto messageDeferral = args.GetDeferral();
    auto messageRequest = args.Request();
    auto message = messageRequest.Message();
    const auto messageCommand = message.Lookup(L"command").as<hstring>();
    const std::wstring_view command { messageCommand };

    if (command == L"queue-requests")
    {
        com_array<hstring> requests;

        message.Lookup(L"requests").as<IPropertyValue>().GetStringArray(requests);

        if (!requests.empty())
        {
            std::unique_lock lock { m_requestQueue.mutex };

            if (!m_requestQueue.shutdown)
            {
                for (const auto& request : requests)
                {
                    m_requestQueue.payloads.push_back(JsonObject::Parse(request));
                }
            }

            lock.unlock();
            m_requestQueue.condition.notify_one();
        }
    }
    else if (command == L"get-responses")
    {
        apartment_context callingThread;

        co_await resume_background();

        std::unique_lock lock { m_responseQueue.mutex };

        m_responseQueue.condition.wait(lock, [this]() noexcept
        {
            return m_responseQueue.shutdown
                || !m_responseQueue.payloads.empty();
        });

        com_array<hstring> responses(static_cast<com_array<hstring>::size_type>(m_responseQueue.payloads.size()));

        std::transform(m_responseQueue.payloads.begin(), m_responseQueue.payloads.end(), responses.begin(),
            [](const JsonObject& response) -> hstring
        {
            return response.ToString();
        });

        if (!m_responseQueue.shutdown)
        {
            const auto itr = std::find_if(m_responseQueue.payloads.begin(), m_responseQueue.payloads.end(),
                [](JsonObject& response)
            {
                const auto type = response.GetNamedString(L"type");

                return (type == L"stopped");
            });

            if (itr != m_responseQueue.payloads.end())
            {
                m_responseQueue.shutdown = true;
            }
        }

        m_responseQueue.payloads.clear();
        lock.unlock();

        co_await callingThread;

        ValueSet returnValue;

        returnValue.Insert(L"responses", PropertyValue::CreateStringArray(responses));

        co_await messageRequest.SendResponseAsync(returnValue);
    }

    messageDeferral.Complete();
}

IAsyncAction App::OnBridgeResponseReceived(AppServiceConnection const& /*sender*/, AppServiceRequestReceivedEventArgs const& args)
{
    auto messageDeferral = args.GetDeferral();
    auto messageRequest = args.Request();
    auto message = messageRequest.Message();
    const auto messageCommand = message.Lookup(L"command").as<hstring>();
    const std::wstring_view command { messageCommand };

    if (command == L"get-requests")
    {
        apartment_context callingThread;

        co_await resume_background();

        std::unique_lock lock { m_requestQueue.mutex };

        m_requestQueue.condition.wait(lock, [this]() noexcept
        {
            return m_requestQueue.shutdown
                || !m_requestQueue.payloads.empty();
        });

        com_array<hstring> requests(static_cast<com_array<hstring>::size_type>(m_requestQueue.payloads.size()));

        std::transform(m_requestQueue.payloads.begin(), m_requestQueue.payloads.end(), requests.begin(),
            [](const JsonObject& request) -> hstring
        {
            return request.ToString();
        });

        m_requestQueue.payloads.clear();
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

        if (!responses.empty())
        {
            bool stopped = false;
            std::unique_lock responseLock { m_responseQueue.mutex };

            if (!m_responseQueue.shutdown)
            {
                for (const auto& response : responses)
                {
                    auto responseObject = JsonObject::Parse(response);

                    if (!stopped)
                    {
                        const auto type = responseObject.GetNamedString(L"type");

                        stopped = (type == L"stopped");
                    }

                    m_responseQueue.payloads.push_back(std::move(responseObject));
                }
            }

            responseLock.unlock();
            m_responseQueue.condition.notify_one();

            if (stopped)
            {
                std::unique_lock requestLock { m_requestQueue.mutex };

                m_requestQueue.shutdown = true;

                requestLock.unlock();
                m_requestQueue.condition.notify_one();
            }
        }
    }

    messageDeferral.Complete();
}
