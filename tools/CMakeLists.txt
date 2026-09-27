cmake_minimum_required(VERSION 3.22.1)
project(sensor_logger_tools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(replay_sampling
    replay_sampling.cc
    ../app/src/main/cpp/pipeline/frame_motion.cc
    ../app/src/main/cpp/pipeline/keyframe_selector.cc
    ../app/src/main/cpp/pipeline/motion_grid_reader.cc
)

target_include_directories(replay_sampling PRIVATE
    ../app/src/main/cpp/camera
    ../app/src/main/cpp/pipeline
)

target_compile_options(replay_sampling PRIVATE -Wall -Wextra -Wpedantic -Werror)
