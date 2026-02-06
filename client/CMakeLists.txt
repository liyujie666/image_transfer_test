cmake_minimum_required(VERSION 3.12)
project(client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SRC_CLIENT 
    client.cpp
    utils/*.cpp
)

add_executable(client ${SRC_CLIENT})

# opencv
find_package(OpenCV REQUIRED)

target_include_directories(client PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}

)
target_link_libraries(client PRIVATE 
    ${OpenCV_LIBS} 
    zmq
    pthread
)

