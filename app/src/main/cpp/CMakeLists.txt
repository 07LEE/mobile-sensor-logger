cmake_minimum_required(VERSION 3.22.1)
project(sensor_logger LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GameActivity ships its C sources through Prefab; building them into our
# library is how the native entry point gets an Activity to hand ARCore.
find_package(game-activity REQUIRED CONFIG)

add_library(sensor_logger SHARED
    native_app.cc
    ar_session.cc
    keyframe_selector.cc
    pending_frame.cc
    sharpness.cc
    session_recorder.cc
)

target_include_directories(sensor_logger PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(sensor_logger
    game-activity::game-activity_static
    arcore
    android
    log
    EGL
    GLESv3
)
