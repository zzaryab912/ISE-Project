cmake_minimum_required(VERSION 3.16)
project(BarcodeApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc imgcodecs videoio objdetect)

add_executable(BarcodeApp main.cpp barcodeGenerator.cpp BarcodePlotter.cpp)
target_include_directories(BarcodeApp PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(BarcodeApp PRIVATE Qt6::Widgets ${OpenCV_LIBS})
