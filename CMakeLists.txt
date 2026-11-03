cmake_minimum_required(VERSION 3.21)
project(Packix VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Test)

# Archive engine without any interface: shared with Folix and, later, the Android app.
add_library(packix_core STATIC
    core/Types.cpp core/Types.h
    core/Shared.cpp core/Shared.h
    core/Formats.cpp core/Formats.h
    core/Volumes.cpp core/Volumes.h
    core/Names.cpp core/Names.h
    core/Engine.h
    core/SevenZipEngine.cpp core/SevenZipEngine.h
    core/Archiver.cpp core/Archiver.h
    core/Temp.cpp core/Temp.h
)
target_include_directories(packix_core PUBLIC core)
target_link_libraries(packix_core PUBLIC Qt6::Core)
# Lets builds that run from the build folder find shared/ in the source tree.
target_compile_definitions(packix_core PRIVATE PACKIX_SOURCE_SHARED="${CMAKE_CURRENT_SOURCE_DIR}/shared")

add_executable(packix-cli cli/main.cpp)
target_link_libraries(packix-cli PRIVATE packix_core)
set_target_properties(packix-cli PROPERTIES OUTPUT_NAME packix)

enable_testing()
add_executable(tst_core tests/tst_core.cpp)
target_link_libraries(tst_core PRIVATE packix_core Qt6::Test)
add_test(NAME core COMMAND tst_core)
