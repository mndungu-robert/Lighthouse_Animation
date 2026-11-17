cmake_minimum_required(VERSION 3.21)

project(lighthouse LANGUAGES CXX)

find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

add_executable(lighthouse
    src/main.cpp
)

target_compile_features(lighthouse PRIVATE cxx_std_17)
target_link_libraries(lighthouse PRIVATE
    OpenGL::GL
    glad::glad
    glfw
    glm::glm
)

if(MSVC)
    target_compile_options(lighthouse PRIVATE /W4 /permissive-)
else()
    target_compile_options(lighthouse PRIVATE -Wall -Wextra -Wpedantic)
endif()
