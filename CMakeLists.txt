cmake_minimum_required(VERSION 3.20)
project(LighthouseScene LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Same libraries as Stage B1 (installed with: vcpkg install glfw3 glad glm stb)
find_package(glfw3 CONFIG REQUIRED)
find_package(glad  CONFIG REQUIRED)
find_package(glm   CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image_write.h")

add_executable(lighthouse src/main.cpp)
target_include_directories(lighthouse PRIVATE src ${STB_INCLUDE_DIRS})
target_link_libraries(lighthouse PRIVATE glfw glad::glad glm::glm)

if(MSVC)
  target_compile_options(lighthouse PRIVATE /utf-8 /W3)
endif()