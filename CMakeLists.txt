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


# Set the working directory to the project root
set_target_properties(lighthouse PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# Copy shaders next to the executable after building
add_custom_command(TARGET lighthouse POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:lighthouse>/shaders"
    COMMENT "Copying shader files to the executable directory"
)
