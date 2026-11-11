cmake_minimum_required(VERSION 3.16)
project(Eyeris LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc imgcodecs videoio objdetect)

add_executable(Eyeris MACOSX_BUNDLE
    main.cpp
    barcodeGenerator.cpp
    BarcodePlotter.cpp
    BarcodeImageIO.cpp
    camera-harness.cpp
    BarcodeDecoder.cpp
)

target_include_directories(Eyeris PRIVATE ${OpenCV_INCLUDE_DIRS})
target_compile_definitions(Eyeris PRIVATE
    EYERIS_SOURCE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
target_link_libraries(Eyeris PRIVATE Qt6::Widgets Qt6::Svg ${OpenCV_LIBS})

if(APPLE)
    set_target_properties(Eyeris PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Eyeris"
        MACOSX_BUNDLE_GUI_IDENTIFIER "edu.eyeris.barcode-studio"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        MACOSX_BUNDLE TRUE
    )
    set(EYERIS_RESOURCE_DIRECTORY "$<TARGET_BUNDLE_CONTENT_DIR:Eyeris>/Resources/assets")
else()
    set(EYERIS_RESOURCE_DIRECTORY "$<TARGET_FILE_DIR:Eyeris>/assets")
endif()

add_custom_command(TARGET Eyeris POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${EYERIS_RESOURCE_DIRECTORY}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${EYERIS_RESOURCE_DIRECTORY}"
    VERBATIM
)
