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
    ../app/src/main/cpp/pipeline/motion_grid_reader.cc
    ../app/src/main/cpp/pipeline/sharpness.cc
)

target_include_directories(sampling_tests PRIVATE
    ../app/src/main/cpp/camera
    ../app/src/main/cpp/pipeline
)

target_compile_options(sampling_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME sampling_tests COMMAND sampling_tests)

add_executable(periodic_schedule_tests
    periodic_schedule_tests.cc
    ../app/src/main/cpp/platform/periodic_schedule.cc
)

target_include_directories(periodic_schedule_tests PRIVATE
    ../app/src/main/cpp/platform
)

target_compile_options(periodic_schedule_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)

add_test(NAME periodic_schedule_tests COMMAND periodic_schedule_tests)

add_executable(timestamp_stats_tests
    timestamp_stats_tests.cc
    ../app/src/main/cpp/pipeline/timestamp_stats.cc
)

target_include_directories(timestamp_stats_tests PRIVATE
    ../app/src/main/cpp/pipeline
)

target_compile_options(timestamp_stats_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)

add_test(NAME timestamp_stats_tests COMMAND timestamp_stats_tests)

add_executable(fps_selection_tests
    fps_selection_tests.cc
    ../app/src/main/cpp/camera/fps_selection.cc
)

target_include_directories(fps_selection_tests PRIVATE
    ../app/src/main/cpp/camera
)

target_compile_options(fps_selection_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)

add_test(NAME fps_selection_tests COMMAND fps_selection_tests)
