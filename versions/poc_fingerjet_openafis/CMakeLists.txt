cmake_minimum_required(VERSION 3.14)
project(fingerjet_openafis_poc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Paths
set(FINGERJET_ROOT "D:/Career/KIPSMA/FingerprintMatcher-References/FingerJetFXOSE/FingerJetFXOSE")
set(OPENAFIS_ROOT "D:/Career/KIPSMA/FingerprintMatcher-References/openafis")
set(OPENCV_ROOT "C:/tools/opencv3411")
set(KIPSMA_ROOT "D:/Career/KIPSMA/kipsma_fingerprint_matcher")

# Include directories
include_directories(
    ${FINGERJET_ROOT}/libFJFX/include
    ${OPENAFIS_ROOT}/lib
    ${OPENAFIS_ROOT}/3rdparty
    ${OPENCV_ROOT}/include
    ${KIPSMA_ROOT}/include
)

# Link directories
link_directories(
    ${FINGERJET_ROOT}/dist/win64/Release
    ${OPENAFIS_ROOT}/build/lib/Release
    ${OPENCV_ROOT}/lib
    ${KIPSMA_ROOT}/build/lib/Release
)

add_executable(poc_matcher main.cpp)

target_link_libraries(poc_matcher PRIVATE
    FJFX
    openafis
    kipsma_fingerprint
    opencv_core3411
    opencv_imgcodecs3411
    opencv_imgproc3411
)

add_executable(benchmark_runner benchmark_runner.cpp)

target_link_libraries(benchmark_runner PRIVATE
    FJFX
    openafis
    kipsma_fingerprint
    opencv_core3411
    opencv_imgcodecs3411
    opencv_imgproc3411
)


