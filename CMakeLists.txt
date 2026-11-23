cmake_minimum_required(VERSION 3.20)
project(LighthouseScene LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS
  NAMES stb_image_write.h
  HINTS /usr/include/stb /usr/local/include/stb
)
if(NOT STB_INCLUDE_DIRS)
  message(FATAL_ERROR "stb_image_write.h was not found")
endif()

add_executable(lighthouse src/main.cpp)

if(WIN32)
  find_package(glad CONFIG REQUIRED)
  target_link_libraries(lighthouse PRIVATE glfw glad::glad glm::glm)
else()
  set(GLAD_SOURCE_DIR "${CMAKE_BINARY_DIR}/generated")
  if(NOT EXISTS "${GLAD_SOURCE_DIR}/src/gl.c")
    find_program(GLAD_EXECUTABLE glad REQUIRED)
    file(MAKE_DIRECTORY "${GLAD_SOURCE_DIR}")
    execute_process(
      COMMAND "${GLAD_EXECUTABLE}" --reproducible --api gl:core=3.3
              --out-path "${GLAD_SOURCE_DIR}" c --loader
      RESULT_VARIABLE GLAD_RESULT
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    if(NOT GLAD_RESULT EQUAL 0)
      message(FATAL_ERROR "Failed to generate GLAD loader")
    endif()
  endif()

  target_sources(lighthouse PRIVATE "${GLAD_SOURCE_DIR}/src/gl.c")
  target_include_directories(lighthouse PRIVATE "${GLAD_SOURCE_DIR}/include")
  find_package(OpenGL REQUIRED)
  target_link_libraries(lighthouse PRIVATE glfw glm::glm OpenGL::GL)
endif()

target_include_directories(lighthouse PRIVATE src ${STB_INCLUDE_DIRS})

if(MSVC)
  target_compile_options(lighthouse PRIVATE /utf-8 /W3)
else()
  target_compile_options(lighthouse PRIVATE -Wall -Wextra -Wpedantic)
endif()