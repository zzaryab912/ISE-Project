cmake_minimum_required(VERSION 3.16)
project(Eyeris LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)
set(EYERIS_OPENCV_COMPONENTS core imgproc imgcodecs videoio objdetect)
find_package(OpenCV 4 QUIET COMPONENTS ${EYERIS_OPENCV_COMPONENTS})
if(NOT OpenCV_FOUND)
    find_package(OpenCV 5 REQUIRED COMPONENTS ${EYERIS_OPENCV_COMPONENTS} geometry)
endif()

add_executable(Eyeris MACOSX_BUNDLE
    src/app/main.cpp
    src/barcode/barcodeGenerator.cpp
    src/barcode/BarcodePlotter.cpp
    src/barcode/BarcodeImageIO.cpp
    src/barcode/camera-harness.cpp
    src/barcode/BarcodeDecoder.cpp
    src/storage/PosStorage.cpp
    src/storage/IdGenerator.cpp
    src/barcode/BarcodeAssetService.cpp
    src/pos/ProductService.cpp
    src/pos/PricingCalculator.cpp
    src/pos/CartService.cpp
    src/barcode/PosScanner.cpp
    src/pos/CheckoutService.cpp
    src/pos/HistoryService.cpp
    src/ui/PosWindow.cpp
)

target_include_directories(Eyeris PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/barcode
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pos
    ${CMAKE_CURRENT_SOURCE_DIR}/src/storage
)
target_compile_definitions(Eyeris PRIVATE
    EYERIS_PROJECT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    EYERIS_SOURCE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
target_link_libraries(Eyeris PRIVATE Qt6::Widgets Qt6::Svg ${OpenCV_LIBS})

if(APPLE)
    file(READ "${CMAKE_ROOT}/Modules/MacOSXBundleInfo.plist.in" EYERIS_INFO_PLIST)
    string(REPLACE "</dict>"
        "<key>NSCameraUsageDescription</key>\n<string>Eyeris uses the camera to scan barcodes.</string>\n</dict>"
        EYERIS_INFO_PLIST "${EYERIS_INFO_PLIST}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/EyerisInfo.plist.in" "${EYERIS_INFO_PLIST}")
    set_target_properties(Eyeris PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/EyerisInfo.plist.in"
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
