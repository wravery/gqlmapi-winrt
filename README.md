# gqlmapi-winrt

This is an experiment in using a [full-trust Win32 bridge](https://docs.microsoft.com/en-us/uwp/api/Windows.ApplicationModel.FullTrustProcessLauncher?view=winrt-19041)
and UWP [AppService](https://docs.microsoft.com/en-us/uwp/api/windows.applicationmodel.appservice?view=winrt-19041) to connect a C++/WinRT UWP app to a Win32 COM server
with [CppGraphQLGen](https://github.com/microsoft/cppgraphqlgen). In this case, I'm using the [GqlMAPI](https://github.com/microsoft/gqlmapi) library to connect to
MAPI.

The default app in this package has a single default [MainPage.xaml](./appservice/MainPage.xaml) which resembles a very basic version of
[eMAPI](https://github.com/microsoft/eMAPI). The next step is to incorporate this into a [React Native for Windows](https://github.com/Microsoft/react-native-windows)
application with a native module wrapped around [clientlib](./clientlib/).

## Getting Started

You'll need to build a NuGet package from [GqlMAPI](https://github.com/microsoft/gqlmapi). Once you have the CMake build directory configured, you can build the package
with a command like this:
```shell
> cmake --build . --config RelWithDebInfo --target package
```

Then open [gqlmapi-winrt.sln](./gqlmapi-winrt.sln) in Visual Studio 2019. Add a local NuGet source directory to Visual Studio pointing to the directory with the NuGet
package, and restore the missing packages for the solution.
