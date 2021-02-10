#pragma once
#include "App.xaml.g.h"

#include <map>
#include <mutex>
#include <vector>

namespace winrt::appservice::implementation
{
    struct PayloadQueue
    {
        std::mutex mutex;
        std::condition_variable condition;
        std::vector<Windows::Data::Json::JsonObject> payloads;
        bool shutdown = false;
    };

    struct App;

    struct ServiceConnection
    {
        explicit ServiceConnection(int connectionId, App& app);

        void OnAppServicesCanceled(Windows::ApplicationModel::Background::IBackgroundTaskInstance const& sender, Windows::ApplicationModel::Background::BackgroundTaskCancellationReason const& reason);
        void OnServiceClosed(Windows::ApplicationModel::AppService::AppServiceConnection const& sender, Windows::ApplicationModel::AppService::AppServiceClosedEventArgs const& reason);

        const int m_connectionId;
        App& m_app;

        Windows::ApplicationModel::AppService::AppServiceConnection m_appServiceConnection { nullptr };
        Windows::ApplicationModel::Background::BackgroundTaskDeferral m_backgroundTaskDeferral { nullptr };

    private:
        void ShutdownService();
    };

    struct App : AppT<App>
    {
        App();

        void OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs const&);
        void OnSuspending(IInspectable const&, Windows::ApplicationModel::SuspendingEventArgs const&);
        void OnNavigationFailed(IInspectable const&, Windows::UI::Xaml::Navigation::NavigationFailedEventArgs const&);

        // App Service
        void OnBackgroundActivated(Windows::ApplicationModel::Activation::BackgroundActivatedEventArgs const&);
        void OnServiceConnectionShutdown(int connectionId);

    private:
        Windows::Foundation::IAsyncAction OnClientRequestReceived(Windows::ApplicationModel::AppService::AppServiceConnection const& sender, Windows::ApplicationModel::AppService::AppServiceRequestReceivedEventArgs const& args);
        Windows::Foundation::IAsyncAction OnBridgeResponseReceived(Windows::ApplicationModel::AppService::AppServiceConnection const& sender, Windows::ApplicationModel::AppService::AppServiceRequestReceivedEventArgs const& args);

        int parsedId = -1;
        std::wstring results;

        PayloadQueue m_requestQueue;
        PayloadQueue m_responseQueue;
        std::map<int, std::unique_ptr<ServiceConnection>> m_serviceConnections;
    };
}
