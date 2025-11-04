#!/bin/sh
set -eu
root=build/linux/dotnet-source
mkdir -p "$root/Infrastructure/Mplx.DotNet" "$root/Presentation/tests-dotnet"
cp NuGet.Config "$root/"
cp Infrastructure/Mplx.DotNet/*.cs Infrastructure/Mplx.DotNet/*.csproj "$root/Infrastructure/Mplx.DotNet/"
cp Presentation/tests-dotnet/*.cs Presentation/tests-dotnet/*.csproj "$root/Presentation/tests-dotnet/"
export DOTNET_CLI_HOME=/workspace/build/linux/dotnet-home
export NUGET_PACKAGES=/workspace/build/linux/nuget
dotnet run --project "$root/Presentation/tests-dotnet" -c Release -p:MplxNativePath=/workspace/build/linux/release/lib/libmplx_native.so -p:MplxNativeFileName=libmplx_native.so
