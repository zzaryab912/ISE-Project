cmake_minimum_required(VERSION 3.16)
project(Eyeris LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)
find_package(Qt6Keychain QUIET)
if(Qt6Keychain_FOUND)
    if(TARGET Qt6Keychain::Qt6Keychain)
        set(EYERIS_QTKEYCHAIN_TARGET Qt6Keychain::Qt6Keychain)
    elseif(TARGET qt6keychain)
        set(EYERIS_QTKEYCHAIN_TARGET qt6keychain)
    else()
        message(FATAL_ERROR "Qt6Keychain was found, but neither Qt6Keychain::Qt6Keychain nor qt6keychain target is defined.")
    endif()
else()
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_TRANSLATIONS OFF CACHE BOOL "" FORCE)
    set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        qtkeychain
        GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
        GIT_TAG 0.18.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(qtkeychain)
    if(TARGET Qt6Keychain::Qt6Keychain)
        set(EYERIS_QTKEYCHAIN_TARGET Qt6Keychain::Qt6Keychain)
    else()
        set(EYERIS_QTKEYCHAIN_TARGET qt6keychain)
    endif()
endif()
set(EYERIS_OPENCV_COMPONENTS core imgproc imgcodecs videoio objdetect)
find_package(OpenCV 4 QUIET COMPONENTS ${EYERIS_OPENCV_COMPONENTS})
if(NOT OpenCV_FOUND)
    find_package(OpenCV 5 REQUIRED COMPONENTS ${EYERIS_OPENCV_COMPONENTS} geometry)
endif()

add_executable(Eyeris MACOSX_BUNDLE
    src/app/main.cpp
    src/app/ManagerPinService.cpp
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
    src/pos/DiscountService.cpp
    src/barcode/PosScanner.cpp
    src/pos/BillPdfService.cpp
    src/pos/CheckoutService.cpp
    src/pos/HistoryService.cpp
    src/pos/ReportService.cpp
    src/ui/PosWindow.cpp
    src/ui/ManagerCataloguePage.cpp
    src/ui/ReportsPage.cpp
)

target_include_directories(Eyeris PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/barcode
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pos
    ${CMAKE_CURRENT_SOURCE_DIR}/src/storage
)
target_compile_definitions(Eyeris PRIVATE
    EYERIS_PROJECT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    EYERIS_SOURCE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
if(MSVC)
    target_compile_options(Eyeris PRIVATE /utf-8)
endif()
target_link_libraries(Eyeris PRIVATE Qt6::Widgets Qt6::Svg ${EYERIS_QTKEYCHAIN_TARGET} ${OpenCV_LIBS})

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
