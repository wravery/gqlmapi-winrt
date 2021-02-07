#include "pch.h"

#include "App.h"
#include "MainPage.h"

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

void App::OnBackgroundActivated(BackgroundActivatedEventArgs const& e)
{
    auto taskInstance = e.TaskInstance();
    auto appServiceTrigger = taskInstance.TriggerDetails().as<AppServiceTriggerDetails>();

    m_backgroundTaskDeferral = taskInstance.GetDeferral();
    taskInstance.Canceled({ get_weak(), &App::OnAppServicesCanceled });

    m_appServiceConnection = appServiceTrigger.AppServiceConnection();
    m_appServiceConnection.RequestReceived({ get_weak(), &App::OnRequestReceived });
    m_appServiceConnection.ServiceClosed({ get_weak(), &App::OnServiceClosed });
}

IAsyncAction App::OnRequestReceived(AppServiceConnection const& /*sender*/, AppServiceRequestReceivedEventArgs const& args)
{
    auto messageDeferral = args.GetDeferral();
    auto message = args.Request().Message();
    const std::wstring_view command { message.Lookup(L"command").as<hstring>() };

    if (command == L"get-requests")
    {
        ValueSet returnValue;

        // Start with canned requests, these should be queued
        if (parsedId < 0)
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

            auto requests = PropertyValue::CreateStringArray({
                startService.ToString(),
                parseQuery.ToString(),
            });

            returnValue.Insert(L"requests", requests);
        }
        else if (results.empty())
        {
            JsonObject fetchQuery;

            fetchQuery.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(2));
            fetchQuery.SetNamedValue(L"type", JsonValue::CreateStringValue(L"fetchQuery"));
            fetchQuery.SetNamedValue(L"queryId", JsonValue::CreateNumberValue(parsedId));

            auto requests = PropertyValue::CreateStringArray({
                fetchQuery.ToString(),
            });

            returnValue.Insert(L"requests", requests);
        }
        else
        {
            JsonObject discardQuery;

            discardQuery.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(3));
            discardQuery.SetNamedValue(L"type", JsonValue::CreateStringValue(L"discardQuery"));
            discardQuery.SetNamedValue(L"queryId", JsonValue::CreateNumberValue(parsedId));

            JsonObject stopService;

            stopService.SetNamedValue(L"requestId", JsonValue::CreateNumberValue(4));
            stopService.SetNamedValue(L"type", JsonValue::CreateStringValue(L"stopService"));

            auto requests = PropertyValue::CreateStringArray({
                discardQuery.ToString(),
                stopService.ToString(),
            });

            returnValue.Insert(L"requests", requests);
        }

        co_await args.Request().SendResponseAsync(returnValue);
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
            }
            else if (requestId == 2 && type == L"complete")
            {
                results = responseObject.GetNamedObject(L"data").ToString();
            }
        }
    }

    messageDeferral.Complete();
}

void App::OnServiceClosed(AppServiceConnection const& /*sender*/, AppServiceClosedEventArgs const& /*reason*/)
{
    ShutdownService();
}

void App::OnAppServicesCanceled(IBackgroundTaskInstance const& /*sender*/, BackgroundTaskCancellationReason const& /*reason*/)
{
    ShutdownService();
}

void App::ShutdownService()
{
    parsedId = -1;
    results.clear();

    m_backgroundTaskDeferral.Complete();
    m_backgroundTaskDeferral = nullptr;
    m_appServiceConnection = nullptr;
}