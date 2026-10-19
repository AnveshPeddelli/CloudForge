cmake_minimum_required(VERSION 3.24)

project(cloudForge VERSION 0.1.0 LANGUAGES CXX)
option(CLOUDFORGE_ENABLE_CUDA "Build CUDA components" OFF)

if(CLOUDFORGE_ENABLE_CUDA)
    enable_language(CUDA)

    add_executable(cloudforge_cuda_check
        apps/cuda_check.cu
    )

    set_target_properties(cloudforge_cuda_check PROPERTIES
    CUDA_STANDARD 20
    CUDA_STANDARD_REQUIRED ON
    CUDA_EXTENSIONS OFF
    )

        add_library(cloudforge_cuda
        src/voxel_grid_cuda.cu
    )

    target_include_directories(cloudforge_cuda PUBLIC include)
    target_compile_features(cloudforge_cuda PUBLIC cxx_std_20)

    set_target_properties(cloudforge_cuda PROPERTIES
        CUDA_STANDARD 20
        CUDA_STANDARD_REQUIRED ON
        CUDA_EXTENSIONS OFF
        CXX_EXTENSIONS OFF
    )

endif()

# Reusable processing library
add_library(cloudforge_core
    src/voxel_grid.cpp
    src/synthetic.cpp
    src/pcd_io.cpp
)

target_include_directories(cloudforge_core PUBLIC include)
target_compile_features(cloudforge_core PUBLIC cxx_std_20)

set_target_properties(cloudforge_core PROPERTIES
    CXX_EXTENSIONS OFF
)

target_compile_options(cloudforge_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# Demo application
add_executable(cloudforge apps/main.cpp)

target_link_libraries(cloudforge PRIVATE cloudforge_core)

if(CLOUDFORGE_ENABLE_CUDA)
    add_executable(cloudforge_compare apps/compare.cpp)
    target_link_libraries(cloudforge_compare PRIVATE
        cloudforge_core cloudforge_cuda)
    set_target_properties(cloudforge_compare PROPERTIES CXX_EXTENSIONS OFF)
endif()

set_target_properties(cloudforge PROPERTIES
    CXX_EXTENSIONS OFF
)

target_compile_options(cloudforge PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# Automated tests
include(CTest)

if(BUILD_TESTING)
    find_package(GTest REQUIRED)

    add_executable(cloudforge_tests
        tests/voxel_grid_test.cpp
    )

    target_link_libraries(cloudforge_tests PRIVATE
        cloudforge_core
        GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(cloudforge_tests)
endif()
