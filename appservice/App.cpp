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

ServiceConnection::ServiceConnection(
    const AppServiceConnection& appServiceConnection,
    const BackgroundTaskDeferral& backgroundTaskDeferral,
    const ServiceRequestHandler& onResponse)
    : m_appServiceConnection { appServiceConnection }
    , m_backgroundTaskDeferral { backgroundTaskDeferral }
    , m_onResponse { onResponse }
{
}

IAsyncOperation<bool> ServiceConnection::SendRequestAsync(const ValueSet& message)
{
    const auto messageResult = co_await m_appServiceConnection.SendMessageAsync(message);
    const auto messageStatus = messageResult.Status();

    co_return messageStatus == AppServiceResponseStatus::Success;
}

IAsyncAction ServiceConnection::OnRequestReceived(const AppServiceConnection& /* sender */, const AppServiceRequestReceivedEventArgs& args)
{
    if (!m_onResponse)
    {
        co_return;
    }

    const auto messageDeferral = args.GetDeferral();
    const auto messageRequest = args.Request();
    const auto message = messageRequest.Message();

    co_await m_onResponse(message);

    messageDeferral.Complete();
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
    m_onResponse = nullptr;
}

void App::OnBackgroundActivated(BackgroundActivatedEventArgs const& e)
{
    auto taskInstance = e.TaskInstance();
    auto taskDeferral = taskInstance.GetDeferral();
    auto appServiceTrigger = taskInstance.TriggerDetails().as<AppServiceTriggerDetails>();
    const auto appServiceConnection = appServiceTrigger.AppServiceConnection();
    const auto appServiceName = appServiceConnection.AppServiceName();
    ServiceRequestHandler onRequest { nullptr };

    if (appServiceName == L"gqlmapi.client")
    {
        onRequest = { this, &App::OnClientRequestReceived };
    }
    else if (appServiceName == L"gqlmapi.bridge")
    {
        onRequest = { this, &App::OnBridgeResponseReceived };
    }

    auto serviceConnection = std::make_unique<ServiceConnection>(appServiceConnection, taskDeferral, onRequest);

    taskInstance.Canceled({ serviceConnection.get(), &ServiceConnection::OnAppServicesCanceled });
    appServiceConnection.ServiceClosed({ serviceConnection.get(), &ServiceConnection::OnServiceClosed });
    appServiceConnection.RequestReceived({ serviceConnection.get(), &ServiceConnection::OnRequestReceived });

    if (appServiceName == L"gqlmapi.client")
    {
        m_clientConnection = std::move(serviceConnection);
    }
    else if (appServiceName == L"gqlmapi.bridge")
    {
        m_bridgeConnection = std::move(serviceConnection);
    }
}

IAsyncOperation<bool> App::OnClientRequestReceived(const ValueSet& message)
{
    if (!m_bridgeConnection)
    {
        co_return false;
    }

    co_return co_await m_bridgeConnection->SendRequestAsync(message);
}

IAsyncOperation<bool> App::OnBridgeResponseReceived(const ValueSet& message)
{
    if (!m_clientConnection)
    {
        co_return false;
    }

    co_return co_await  m_clientConnection->SendRequestAsync(message);
}
