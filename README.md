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

Adjust the Qt and OpenCV paths for other installations. The team-written encoder supplies Code 128 Set B bits to the OpenCV renderer; the UI converts its result to `QImage` for display and PNG saving. `camera-harness.cpp` loads PNG/JPEG files, captures camera frames and prepares grayscale candidates for the team-written decoder. Scan one barcode at a time and keep the white margins visible.

Source files and the shared `ReaderContracts.h` live at the repository root. The decoder is compiled once. No barcode encoding or decoding library is used.

### Branching & Contribution
- Keep `main` stable.
- Use `iteration_1` as the shared Increment 1 branch and work in individual branches.
- Coordinate integration of the individual components with the team.
