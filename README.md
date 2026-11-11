# ISE Project

Collaborative repository for the ISE coursework project.

## Overview
This repository contains the source code, documentation, and assets for the project. Access is currently private while specifications and project requirements are being finalized.

## Getting Started
Increment 1 uses C++17, Qt 6 Widgets and SVG, and OpenCV 4. The team writes the Code 128 encoder and decoder itself.

On the demo Mac, configure and build with:

```sh
cmake -S . -B build/eyeris-ui \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.0/macos" \
  -DOpenCV_DIR=/opt/homebrew/opt/opencv@4/lib/cmake/opencv4
cmake --build build/eyeris-ui --parallel 4
./build/eyeris-ui/Eyeris.app/Contents/MacOS/Eyeris
```

Adjust the Qt and OpenCV paths for other installations. Generation and PNG saving use the existing encoder and renderer. Image preparation and camera capture in `camera-harness.cpp` are currently placeholders; those screens display the backend's status until the team's reader implementation is integrated.

### Branching & Contribution
- Keep `main` stable.
- Use `iteration_1` as the shared Increment 1 branch and work in individual branches.
- Coordinate integration of the individual components with the team.
