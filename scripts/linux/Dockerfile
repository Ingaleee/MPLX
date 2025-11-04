FROM mcr.microsoft.com/dotnet/sdk:10.0.302
RUN apt-get update && apt-get install -y --no-install-recommends clang cmake ninja-build g++ nodejs npm && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace
RUN apt-get update -qq && apt-get install -y --no-install-recommends libclang-rt-18-dev && rm -rf /var/lib/apt/lists/*
