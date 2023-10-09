cmake_minimum_required(VERSION 3.8)
project(seqlock-queue)

option(SEQLOCK_QUEUE_BUILD_TESTS "Build the tests" OFF)
option(SEQLOCK_SANITIZE_ADDRESS "Enable address sanitizer in tests" OFF)
option(SEQLOCK_SANITIZE_THREAD "Enable thread sanitizer in tests" OFF)

if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif ()

# address sanitizer flags
if (SEQLOCK_SANITIZE_ADDRESS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined -fno-omit-frame-pointer -g")
endif ()

# thread sanitizer flags
if (SEQLOCK_SANITIZE_THREAD)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread")
endif ()

# library name
set(TARGET_NAME seqlock_queue)

# header files
set(HEADER_FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/seqlock_queue/seqlock_queue.h)

# Add this as a library
add_library(${TARGET_NAME} INTERFACE)

# Add target sources
target_sources(${TARGET_NAME} INTERFACE ${HEADER_FILES})

# Add include directories for this library
target_include_directories(${TARGET_NAME} INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

if (SEQLOCK_QUEUE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif ()