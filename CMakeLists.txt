cmake_minimum_required(VERSION 3.26)
project(mplx LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(Domain/mplx-lang)
add_subdirectory(Application/mplx-compiler)
add_subdirectory(Application/mplx-vm)
add_subdirectory(Presentation/tools/mplx)

# Optional modules (require external deps) — disabled for demo build
# add_subdirectory(Presentation/tests-cpp)
# add_subdirectory(Infrastructure/mplx-orm)
# add_subdirectory(Presentation/benchmarks-cpp)
# add_subdirectory(Infrastructure/mplx-net)
# add_subdirectory(Presentation/tools/mplx-net)
# add_subdirectory(Presentation/tools/mplx-netclient)
# add_subdirectory(Infrastructure/mplx-pkg)
add_subdirectory(Infrastructure/mplx-capi)

