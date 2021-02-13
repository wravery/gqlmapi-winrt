#pragma once
#include "App.xaml.g.h"

#include <vector>

namespace winrt::appservice::implementation
{
    struct ServiceConnection : implements<ServiceConnection, IInspectable>
    {
        ServiceConnection() = default;

        explicit ServiceConnection(
            const Windows::ApplicationModel::AppService::AppServiceConnection& appServiceConnection,
            const Windows::ApplicationModel::Background::BackgroundTaskDeferral& backgroundTaskDeferral,
            const ServiceRequestHandler& onResponse, const ServiceShutdownHandler& onShutdown);

        fire_and_forget SendRequestAsync(const Windows::Foundation::Collections::ValueSet& message);

        void OnAppServicesCanceled(Windows::ApplicationModel::Background::IBackgroundTaskInstance const& sender, Windows::ApplicationModel::Background::BackgroundTaskCancellationReason const& reason);
        void OnServiceClosed(Windows::ApplicationModel::AppService::AppServiceConnection const& sender, Windows::ApplicationModel::AppService::AppServiceClosedEventArgs const& reason);
        Windows::Foundation::IAsyncAction OnRequestReceived(Windows::ApplicationModel::AppService::AppServiceConnection const& sender, Windows::ApplicationModel::AppService::AppServiceRequestReceivedEventArgs const& args);

    private:
        void ShutdownService();

        Windows::ApplicationModel::AppService::AppServiceConnection m_appServiceConnection;
        Windows::ApplicationModel::Background::BackgroundTaskDeferral m_backgroundTaskDeferral;
        ServiceRequestHandler m_onResponse;
        ServiceShutdownHandler m_onShutdown;
    };

    struct App : AppT<App>
    {
        App();

        void OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs const&);
        void OnSuspending(IInspectable const&, Windows::ApplicationModel::SuspendingEventArgs const&);
        void OnNavigationFailed(IInspectable const&, Windows::UI::Xaml::Navigation::NavigationFailedEventArgs const&);

        // App Service
        void OnBackgroundActivated(Windows::ApplicationModel::Activation::BackgroundActivatedEventArgs const&);

    private:
        Windows::Foundation::IAsyncAction OnClientRequestReceived(const Windows::Foundation::Collections::ValueSet& message);
        Windows::Foundation::IAsyncAction OnBridgeResponseReceived(const Windows::Foundation::Collections::ValueSet& message);
        void OnClientShutdown();
        void OnBridgeShutdown();

        bool m_bridgeStarted = false;
        std::vector<Windows::Foundation::Collections::ValueSet> m_bridgeQueue;

        com_ptr<ServiceConnection> m_clientConnection;
        com_ptr<ServiceConnection> m_bridgeConnection;
    };
}
