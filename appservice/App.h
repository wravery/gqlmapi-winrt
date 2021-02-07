#pragma once
#include "App.xaml.g.h"

namespace winrt::appservice::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs const&);
        void OnSuspending(IInspectable const&, Windows::ApplicationModel::SuspendingEventArgs const&);
        void OnNavigationFailed(IInspectable const&, Windows::UI::Xaml::Navigation::NavigationFailedEventArgs const&);

        // App Service
        void OnBackgroundActivated(Windows::ApplicationModel::Activation::BackgroundActivatedEventArgs const&);

    private:
        void OnAppServicesCanceled(Windows::ApplicationModel::Background::IBackgroundTaskInstance const& sender, Windows::ApplicationModel::Background::BackgroundTaskCancellationReason const& reason);
        Windows::Foundation::IAsyncAction OnRequestReceived(Windows::ApplicationModel::AppService::AppServiceConnection const& sender, Windows::ApplicationModel::AppService::AppServiceRequestReceivedEventArgs const& args);
        void OnServiceClosed(Windows::ApplicationModel::AppService::AppServiceConnection const& sender, Windows::ApplicationModel::AppService::AppServiceClosedEventArgs const& reason);

        void ShutdownService();

        int parsedId = -1;
        std::wstring results;

        Windows::ApplicationModel::AppService::AppServiceConnection m_appServiceConnection { nullptr };
        Windows::ApplicationModel::Background::BackgroundTaskDeferral m_backgroundTaskDeferral { nullptr };
    };
}
