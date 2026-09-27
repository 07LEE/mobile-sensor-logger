cmake_minimum_required(VERSION 3.22.1)
project(sensor_logger_host_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_executable(sampling_tests
    sampling_tests.cc
    ../app/src/main/cpp/pipeline/frame_motion.cc
    ../app/src/main/cpp/pipeline/keyframe_selector.cc
    ../app/src/main/cpp/pipeline/sharpness.cc
)

target_include_directories(sampling_tests PRIVATE
    ../app/src/main/cpp/camera
    ../app/src/main/cpp/pipeline
)

target_compile_options(sampling_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME sampling_tests COMMAND sampling_tests)
