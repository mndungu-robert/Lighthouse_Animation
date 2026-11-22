# Lighthouse

A C++17 and OpenGL starter project for the lighthouse scene.

## Windows setup

1. Install CMake, Git, and either MSYS2 with MinGW-w64 or the Visual Studio
   Build Tools with the **Desktop development with C++** workload.
2. Install vcpkg and set the `VCPKG_ROOT` environment variable to its
   installation directory, then restart VS Code so it can read the variable.
3. Add the selected compiler tools to `PATH` (for MSYS2, this is typically
   `C:\msys64\ucrt64\bin`).
4. Open this folder in VS Code and install the recommended extensions.
5. In CMake Tools, select a compiler kit and configure the project. The vcpkg
   manifest installs GLFW, GLAD, GLM, and stb during configuration.
6. Build and run the `lighthouse` target from the CMake Tools status bar.

The starter opens a GLFW window with an OpenGL 3.3 core context and a clear
color. `shaders/basic.vert` and `shaders/basic.frag` are starting points for
the rendering stages; shader loading and scene geometry are not implemented
yet.
