cmake_minimum_required(VERSION 3.16)
project(w-vids LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Network)

include(FetchContent)
set(NFD_BUILD_TESTS OFF CACHE BOOL "Build Native File Dialog examples")
set(NFD_INSTALL OFF CACHE BOOL "Install Native File Dialog")
FetchContent_Declare(nativefiledialog
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG 65b2f070a0fcfee0d453a64e770a217ab0b6ff4f # v1.4.1
)
FetchContent_MakeAvailable(nativefiledialog)

add_executable(w-vids
    src/main.cpp
    src/ffmpeg_installer.h
    src/ffmpeg_installer.cpp
    src/ffmpeg_setup_dialog.h
    src/ffmpeg_setup_dialog.cpp
    src/window.h
    src/window.cpp
    src/player.h
    src/player.cpp
    src/seek_slider.h
    src/seek_slider.cpp
)
target_compile_features(w-vids PRIVATE cxx_std_17)
target_link_libraries(w-vids PRIVATE
    Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets Qt6::Network
    nfd::nfd
)
set_target_properties(w-vids PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

option(WVIDS_BUILD_TESTS "Build interactive player regression checks" OFF)
if(WVIDS_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(fake_ffmpeg tests/fake_ffmpeg.cpp)
    add_executable(ffmpeg_installer_test
        tests/ffmpeg_installer_test.cpp
        src/ffmpeg_installer.cpp
        src/ffmpeg_installer.h
    )
    target_compile_features(ffmpeg_installer_test PRIVATE cxx_std_17)
    target_include_directories(ffmpeg_installer_test PRIVATE src)
    target_link_libraries(ffmpeg_installer_test PRIVATE Qt6::Network Qt6::Test)
    set_target_properties(ffmpeg_installer_test PROPERTIES
        AUTOMOC ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    set_target_properties(fake_ffmpeg PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-tools"
    )
    add_executable(ffmpeg_setup_dialog_test
        tests/ffmpeg_setup_dialog_test.cpp
        src/ffmpeg_setup_dialog.cpp
        src/ffmpeg_setup_dialog.h
        src/ffmpeg_installer.cpp
        src/ffmpeg_installer.h
    )
    target_compile_features(ffmpeg_setup_dialog_test PRIVATE cxx_std_17)
    target_include_directories(ffmpeg_setup_dialog_test PRIVATE src)
    target_link_libraries(ffmpeg_setup_dialog_test PRIVATE Qt6::Widgets Qt6::Network Qt6::Test)
    set_target_properties(ffmpeg_setup_dialog_test PROPERTIES
        AUTOMOC ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    add_executable(player_seek_test
        tests/player_seek_test.cpp
        src/player.cpp
        src/seek_slider.cpp
    )
    target_compile_features(player_seek_test PRIVATE cxx_std_17)
    target_include_directories(player_seek_test PRIVATE src)
    target_link_libraries(player_seek_test PRIVATE
        Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets Qt6::Test
    )
    set_target_properties(player_seek_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()
