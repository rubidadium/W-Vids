cmake_minimum_required(VERSION 3.16)
project(BlackQtWindow LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets)

add_executable(black_qt_window
    src/main.cpp
    src/window.h
    src/window.cpp
    src/player.h
    src/player.cpp
)
target_compile_features(black_qt_window PRIVATE cxx_std_17)
target_link_libraries(black_qt_window PRIVATE
    Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets
)
set_target_properties(black_qt_window PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Keep the default video next to the application, independent of the launch directory.
add_custom_command(TARGET black_qt_window POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:black_qt_window>/asset"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/asset/test.mp4"
        "$<TARGET_FILE_DIR:black_qt_window>/asset/test.mp4"
    VERBATIM
)
