cmake_minimum_required(VERSION 3.22.1)
project(sensor_logger LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GameActivity ships its C sources through Prefab; building them into our
# library is how the native entry point gets a window and an event loop.
find_package(game-activity REQUIRED CONFIG)

# GameActivity's JNI entry point lives in a static library and nothing in our
# code calls it, so the linker would drop it and the activity would fail to
# start with UnsatisfiedLinkError. Naming it as undefined keeps it, and the rest
# of the glue comes in with it.
set(CMAKE_SHARED_LINKER_FLAGS
    "${CMAKE_SHARED_LINKER_FLAGS} -u Java_com_google_androidgamesdk_GameActivity_initializeNativeCode")

add_library(sensor_logger SHARED
    native_app.cc
    camera/camera_source.cc
    camera/capture_config.cc
    pipeline/frame_motion.cc
    pipeline/pending_frame.cc
    pipeline/sharpness.cc
    ui/gl_quad.cc
    ui/preview_renderer.cc
    ui/ui_button.cc
    ui/sessions_overlay.cc
    ui/pro_panel.cc
    storage/frame_writer.cc
    storage/session_recorder.cc
    sensors/imu_source.cc
    input/input.cc
)

target_include_directories(sensor_logger PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/camera
    ${CMAKE_CURRENT_SOURCE_DIR}/sensors
    ${CMAKE_CURRENT_SOURCE_DIR}/storage
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline
    ${CMAKE_CURRENT_SOURCE_DIR}/input
)

# Android 15 and newer expect native libraries aligned to a 16 KB page, and warn
# on install otherwise. The platform and libc++ binaries already are; this is
# what brings ours in line.
target_link_options(sensor_logger PRIVATE "-Wl,-z,max-page-size=16384")

target_link_libraries(sensor_logger
    game-activity::game-activity_static
    camera2ndk
    mediandk
    android
    log
    EGL
    GLESv3
)
