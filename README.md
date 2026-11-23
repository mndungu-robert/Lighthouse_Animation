# Lighthouse Navigation Simulation

A realistic 3D lighthouse simulation developed using C++17 and OpenGL.

The scene consists of a lighthouse located on an island surrounded by a large water body. A boat approaches the shore while being guided by a rotating lighthouse beam. The environment includes realistic lighting, reflections, shadows, animated water, and a dynamic day-night cycle.

---

## Preview

### Sunset
![Sunset](preview/B2_sunset.png)

### Night
![Night](preview/B2_night.png)

### Lighthouse Beam Tracking Boat
![Beam](preview/B3_boat_lit_by_beam.png)

---

## Features

- Realistic lighthouse environment
- Dynamic day and night cycle
- Animated boat navigation and docking
- Rotating lighthouse beam
- Dynamic lighting and shadows
- Water reflections and wave simulation
- HDR rendering and bloom effects
- Interactive camera controls
- Wireframe rendering mode

---

## Built With

- C++17
- OpenGL 3.3
- GLFW
- GLAD
- GLM
- stb_image
- CMake

---

## Running the Project

### Requirements

- Windows 10/11
- OpenGL 3.3 compatible GPU
- CMake 3.15+
- C++17 compiler
- Visual Studio Build Tools or MSYS2/MinGW

### Build

```bash
cmake -B build
cmake --build build
```
### Run

```bash
.\build\Debug\lighthouse.exe
```

Or launch the project directly through the VS Code CMake Tools extension.

## Controls

| Key | Action |
|------|---------|
| Mouse Drag | Rotate/Orbit Camera |
| Scroll Wheel | Zoom In / Out |
| W | Toggle Wireframe Mode |
| 1 | Set Time to Dawn |
| 2 | Set Time to Noon |
| 3 | Set Time to Sunset |
| 4 | Set Time to Night |
| T | Toggle Automatic Day/Night Cycle |
| Space | Pause / Resume Animation |
| R | Restart Boat Animation |
| F | Fast Forward Boat Movement |
| C | Follow Boat Camera |
| Z | Slow Lighthouse Beam Rotation |
| X | Speed Up Lighthouse Beam Rotation |
| L | Force Lighthouse Lamp On |
| P | Save Screenshot |
| ESC | Exit Application |