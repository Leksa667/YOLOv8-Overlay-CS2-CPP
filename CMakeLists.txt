cmake_minimum_required(VERSION 3.20)
project(YOLOv8_Overlay_CS2 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── OpenCV ───────────────────────────────────────────────────────────────────
# Install via vcpkg:  vcpkg install opencv4:x64-windows
# Then pass:  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
find_package(OpenCV REQUIRED)

# ─── ONNX Runtime ─────────────────────────────────────────────────────────────
# Download the pre-built package from:
#   https://github.com/microsoft/onnxruntime/releases
# For CUDA acceleration use the *-gpu package.
# Then set:  -DONNXRUNTIME_DIR=C:/path/to/onnxruntime-win-x64-gpu-1.x.x
#
# vcpkg alternative:  vcpkg install onnxruntime:x64-windows
#   (CPU-only; for GPU build onnxruntime from source or use the pre-built GPU release)
set(ONNXRUNTIME_DIR "" CACHE PATH "Root of the ONNX Runtime installation directory")

if(NOT ONNXRUNTIME_DIR)
    # Try vcpkg auto-detection first
    find_package(onnxruntime CONFIG QUIET)
    if(NOT onnxruntime_FOUND)
        message(FATAL_ERROR
            "ONNX Runtime not found.\n"
            "Either:\n"
            "  1. Install via vcpkg (vcpkg install onnxruntime:x64-windows)\n"
            "     and pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake\n"
            "  2. Download the pre-built release from GitHub and pass:\n"
            "     -DONNXRUNTIME_DIR=<path/to/onnxruntime-win-x64-*>")
    endif()
endif()

# ─── Sources ──────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/screen_capture.cpp
    src/yolo_detector.cpp
)

add_executable(${PROJECT_NAME} WIN32 ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    src/
)

# Link OpenCV
target_link_libraries(${PROJECT_NAME} PRIVATE ${OpenCV_LIBS})

# Link ONNX Runtime (manual path) or vcpkg target
if(ONNXRUNTIME_DIR)
    target_include_directories(${PROJECT_NAME} PRIVATE
        "${ONNXRUNTIME_DIR}/include")
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "${ONNXRUNTIME_DIR}/lib/onnxruntime.lib")

    # Copy DLLs to the output folder automatically
    foreach(dll onnxruntime.dll onnxruntime_providers_shared.dll onnxruntime_providers_cuda.dll)
        if(EXISTS "${ONNXRUNTIME_DIR}/lib/${dll}")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ONNXRUNTIME_DIR}/lib/${dll}"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
        endif()
    endforeach()
else()
    # vcpkg-found target
    target_link_libraries(${PROJECT_NAME} PRIVATE onnxruntime::onnxruntime)
endif()

# Windows system libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    user32 gdi32 gdiplus winmm)

# ─── Compiler options ─────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3 /O2)
    # Unicode build (W functions instead of A)
    target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE)
endif()
