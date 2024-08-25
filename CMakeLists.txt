cmake_minimum_required(VERSION 3.22)

project(protoDeco)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    #Linux-specific code or configuration here
    message(STATUS "Configuring for Linux")
    set(CXX_STANDARD 23)

    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wshadow -ggdb -g")
    # set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../debug)

    add_executable(${PROJECT_NAME}
        src/main.cpp
        # src/mempb.cpp
        deps/HkXML/src/HkXml.cpp
        src/Utility.cpp
        )

    target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
    
    # Needed for absolute include paths
    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR})

    # sudo apt-get install zlib1g-dev (z)
    # sudo apt-get install libminizip-dev
    target_link_libraries(${PROJECT_NAME} z minizip)

# If the operating system is not recognized
else()
    message(FATAL_ERROR "Unsupported operating system: ${CMAKE_SYSTEM_NAME}")
endif()