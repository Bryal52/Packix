cmake_minimum_required(VERSION 3.21)
project(Packix VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(APPLE)
    enable_language(OBJCXX)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Test)

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

# Windows, dialogs and the system layer: also what Folix will take over.
set(PACKIX_UI_SOURCES
    dialogs/Lang.cpp dialogs/Lang.h
    dialogs/Theme.cpp dialogs/Theme.h
    dialogs/Icons.cpp dialogs/Icons.h
    dialogs/Log.cpp dialogs/Log.h
    dialogs/LogViewer.cpp dialogs/LogViewer.h
    dialogs/Effects.cpp dialogs/Effects.h
    dialogs/Thermometer.cpp dialogs/Thermometer.h
    dialogs/PathField.cpp dialogs/PathField.h
    dialogs/Dialogs.cpp dialogs/Dialogs.h
    dialogs/Jobs.cpp dialogs/Jobs.h
    dialogs/CreateDialog.cpp dialogs/CreateDialog.h
    dialogs/ExtractDialog.cpp dialogs/ExtractDialog.h
    dialogs/Actions.cpp dialogs/Actions.h
    platform/Platform.h
)
if(APPLE)
    list(APPEND PACKIX_UI_SOURCES platform/macos/Platform_mac.mm)
    set_source_files_properties(platform/macos/Platform_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()
add_library(packix_ui STATIC ${PACKIX_UI_SOURCES})
target_include_directories(packix_ui PUBLIC dialogs platform)
target_link_libraries(packix_ui PUBLIC packix_core Qt6::Widgets Qt6::Concurrent)
if(APPLE)
    target_link_libraries(packix_ui PUBLIC
        "-framework AppKit" "-framework Security" "-framework UniformTypeIdentifiers" "-framework UserNotifications")
endif()

add_executable(packix-cli cli/main.cpp)
target_link_libraries(packix-cli PRIVATE packix_core)
set_target_properties(packix-cli PROPERTIES OUTPUT_NAME packix)

enable_testing()
add_executable(tst_core tests/tst_core.cpp)
target_link_libraries(tst_core PRIVATE packix_core Qt6::Test)
add_test(NAME core COMMAND tst_core)
add_executable(tst_ui tests/tst_ui.cpp)
target_link_libraries(tst_ui PRIVATE packix_ui Qt6::Test)
target_compile_definitions(tst_ui PRIVATE PACKIX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME ui COMMAND tst_ui)

# ---------------------------------------------------------------------------
# Packix.app

add_executable(makeicon tools/makeicon.cpp)
target_link_libraries(makeicon PRIVATE Qt6::Gui)
add_executable(makedocicon tools/makedocicon.cpp)
target_link_libraries(makedocicon PRIVATE Qt6::Gui)

set(PACKIX_APP_SOURCES
    app/main.cpp
    app/App.cpp app/App.h
    app/ArchiveModel.cpp app/ArchiveModel.h
    app/ArchiveWindow.cpp app/ArchiveWindow.h
    app/SettingsWindow.cpp app/SettingsWindow.h
)

if(APPLE)
    set(LOGO ${CMAKE_CURRENT_SOURCE_DIR}/resources/logo.png)
    set(ICONS_DIR ${CMAKE_CURRENT_BINARY_DIR}/icons)
    set(APP_ICNS ${ICONS_DIR}/AppIcon.icns)
    add_custom_command(
        OUTPUT ${APP_ICNS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ICONS_DIR}
        COMMAND $<TARGET_FILE:makeicon> ${LOGO} ${ICONS_DIR}/AppIcon-1024.png
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tools/png_to_icns.sh ${ICONS_DIR}/AppIcon-1024.png ${APP_ICNS}
        DEPENDS makeicon ${LOGO} tools/png_to_icns.sh
        COMMENT "Building AppIcon.icns")
    set(BUNDLE_ICONS ${APP_ICNS})
    foreach(kind zip 7z rar tar)
        string(TOUPPER ${kind} label)
        set(out ${ICONS_DIR}/doc-${kind}.icns)
        add_custom_command(
            OUTPUT ${out}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${ICONS_DIR}
            COMMAND $<TARGET_FILE:makedocicon> ${LOGO} ${label} ${ICONS_DIR}/doc-${kind}.png
            COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tools/png_to_icns.sh ${ICONS_DIR}/doc-${kind}.png ${out}
            DEPENDS makedocicon ${LOGO} tools/png_to_icns.sh
            COMMENT "Building doc-${kind}.icns")
        list(APPEND BUNDLE_ICONS ${out})
    endforeach()
    set_source_files_properties(${BUNDLE_ICONS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources GENERATED TRUE)

    add_executable(Packix MACOSX_BUNDLE ${PACKIX_APP_SOURCES} ${BUNDLE_ICONS})
    set_target_properties(Packix PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME Packix
        MACOSX_BUNDLE_GUI_IDENTIFIER com.umid.packix
        MACOSX_BUNDLE_ICON_FILE AppIcon.icns
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})

    # Everything the app reads at run time goes inside the bundle: data files, the engine, its license.
    find_program(SEVENZIP 7zz HINTS /opt/homebrew/bin /usr/local/bin REQUIRED)
    add_custom_command(TARGET Packix POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/shared
                $<TARGET_BUNDLE_CONTENT_DIR:Packix>/Resources/shared
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/resources/7zip-License.txt
                $<TARGET_BUNDLE_CONTENT_DIR:Packix>/Resources/7zip-License.txt
        COMMAND ${CMAKE_COMMAND} -E copy ${SEVENZIP} $<TARGET_BUNDLE_CONTENT_DIR:Packix>/MacOS/7zz)
else()
    add_executable(Packix WIN32 ${PACKIX_APP_SOURCES})
endif()

target_include_directories(Packix PRIVATE app)
target_compile_definitions(Packix PRIVATE PACKIX_VERSION="${PROJECT_VERSION}")
target_link_libraries(Packix PRIVATE packix_ui)

# Development helper: renders windows to PNG for a look at the design.
add_executable(previews EXCLUDE_FROM_ALL tools/previews.cpp app/ArchiveModel.cpp app/ArchiveWindow.cpp)
target_include_directories(previews PRIVATE app)
target_link_libraries(previews PRIVATE packix_ui)
