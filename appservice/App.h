#pragma once
#include "App.xaml.g.h"

#include <map>
#include <memory>
#include <vector>

namespace winrt::appservice::implementation
{
    struct ServiceConnection
    {
        explicit ServiceConnection(
            const Windows::ApplicationModel::AppService::AppServiceConnection& appServiceConnection,
            const Windows::ApplicationModel::Background::BackgroundTaskDeferral& backgroundTaskDeferral,
            const ServiceRequestHandler& onResponse);

        Windows::Foundation::IAsyncOperation<bool> SendRequestAsync(const Windows::Foundation::Collections::ValueSet& message);

        void OnAppServicesCanceled(Windows::ApplicationModel::Background::IBackgroundTaskInstance const& sender, Windows::ApplicationModel::Background::BackgroundTaskCancellationReason const& reason);
        void OnServiceClosed(Windows::ApplicationModel::AppService::AppServiceConnection const& sender, Windows::ApplicationModel::AppService::AppServiceClosedEventArgs const& reason);
        Windows::Foundation::IAsyncAction OnRequestReceived(Windows::ApplicationModel::AppService::AppServiceConnection const& sender, Windows::ApplicationModel::AppService::AppServiceRequestReceivedEventArgs const& args);

    private:
        void ShutdownService();

        Windows::ApplicationModel::AppService::AppServiceConnection m_appServiceConnection;
        Windows::ApplicationModel::Background::BackgroundTaskDeferral m_backgroundTaskDeferral;
        ServiceRequestHandler m_onResponse;
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
        Windows::Foundation::IAsyncOperation<bool> OnClientRequestReceived(const Windows::Foundation::Collections::ValueSet& message);
        Windows::Foundation::IAsyncOperation<bool> OnBridgeResponseReceived(const Windows::Foundation::Collections::ValueSet& message);

        std::unique_ptr<ServiceConnection> m_clientConnection;
        std::unique_ptr<ServiceConnection> m_bridgeConnection;
    };
}
