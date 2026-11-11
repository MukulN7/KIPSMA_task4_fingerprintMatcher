cmake_minimum_required(VERSION 3.16)
project(kipsma_fingerprint_matcher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KIPSMA_FP_BUILD_SHARED "Build kipsma_fingerprint as a shared library (DLL)" ON)
option(KIPSMA_FP_BUILD_CLI    "Build standalone CLI executable"                   ON)
option(KIPSMA_FP_BUILD_TESTS  "Build automated test suite"                        ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------- OpenCV
find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs)

if(NOT OpenCV_FOUND)
    # Fallback search for OpenCV 3.4.11 / custom installations on Windows
    find_path(OpenCV_INCLUDE_DIRS "opencv2/opencv.hpp"
        PATHS "${OpenCV_DIR}/include" "${OpenCV_DIR}/../include" "$ENV{OpenCV_DIR}/include" "C:/tools/opencv3411/include"
        NO_DEFAULT_PATH)
    if(NOT OpenCV_INCLUDE_DIRS)
        find_path(OpenCV_INCLUDE_DIRS "opencv2/opencv.hpp")
    endif()

    find_library(OpenCV_CORE_LIB NAMES opencv_core3411 opencv_core
        PATHS "${OpenCV_DIR}/lib" "${OpenCV_DIR}" "$ENV{OpenCV_DIR}/lib" "C:/tools/opencv3411/lib"
        NO_DEFAULT_PATH)
    find_library(OpenCV_IMGPROC_LIB NAMES opencv_imgproc3411 opencv_imgproc
        PATHS "${OpenCV_DIR}/lib" "${OpenCV_DIR}" "$ENV{OpenCV_DIR}/lib" "C:/tools/opencv3411/lib"
        NO_DEFAULT_PATH)
    find_library(OpenCV_IMGCODECS_LIB NAMES opencv_imgcodecs3411 opencv_imgcodecs
        PATHS "${OpenCV_DIR}/lib" "${OpenCV_DIR}" "$ENV{OpenCV_DIR}/lib" "C:/tools/opencv3411/lib"
        NO_DEFAULT_PATH)

    if(OpenCV_CORE_LIB AND OpenCV_IMGPROC_LIB AND OpenCV_INCLUDE_DIRS)
        set(OpenCV_LIBS ${OpenCV_CORE_LIB} ${OpenCV_IMGPROC_LIB} ${OpenCV_IMGCODECS_LIB})
        set(OpenCV_FOUND TRUE)
        message(STATUS "Found OpenCV via fallback: ${OpenCV_LIBS}")
    else()
        message(FATAL_ERROR "Could NOT find OpenCV installation. Please set OpenCV_DIR.")
    endif()
endif()

# ------------------------------------------------------------- Library Target
set(FP_SOURCES
    src/fingerprint_api.cpp
    src/enhancer.cpp
    src/extractor.cpp
    src/matcher.cpp
    src/triplet.cpp
    src/preprocessor.cpp
)

if(KIPSMA_FP_BUILD_SHARED)
    add_library(kipsma_fingerprint SHARED ${FP_SOURCES})
    target_compile_definitions(kipsma_fingerprint PRIVATE KIPSMA_FP_BUILDING_DLL)
else()
    add_library(kipsma_fingerprint STATIC ${FP_SOURCES})
    target_compile_definitions(kipsma_fingerprint PUBLIC KIPSMA_FP_STATIC)
endif()

# ------------------------------------------------------------- FingerJetFX & OpenAFIS Dependencies
if(NOT DEFINED FINGERJET_ROOT)
    if(DEFINED ENV{FINGERJET_ROOT})
        set(FINGERJET_ROOT "$ENV{FINGERJET_ROOT}")
    else()
        set(FINGERJET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../FingerprintMatcher-References/FingerJetFXOSE/FingerJetFXOSE" CACHE PATH "Path to FingerJetFX OSE root directory")
    endif()
endif()

if(NOT DEFINED OPENAFIS_ROOT)
    if(DEFINED ENV{OPENAFIS_ROOT})
        set(OPENAFIS_ROOT "$ENV{OPENAFIS_ROOT}")
    else()
        set(OPENAFIS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../FingerprintMatcher-References/openafis" CACHE PATH "Path to OpenAFIS root directory")
    endif()
endif()

target_include_directories(kipsma_fingerprint
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE src
            ${FINGERJET_ROOT}/libFJFX/include
            ${OPENAFIS_ROOT}/lib
            ${OPENAFIS_ROOT}/3rdparty
)
target_include_directories(kipsma_fingerprint PUBLIC ${OpenCV_INCLUDE_DIRS})
target_link_directories(kipsma_fingerprint PRIVATE
    ${FINGERJET_ROOT}/dist/win64/Release
    ${OPENAFIS_ROOT}/build/lib/Release
)
target_link_libraries(kipsma_fingerprint PUBLIC ${OpenCV_LIBS} PRIVATE FJFX openafis)

if(MSVC)
    target_compile_options(kipsma_fingerprint PRIVATE /W4 /utf-8)
else()
    target_compile_options(kipsma_fingerprint PRIVATE -Wall -Wextra)
endif()

# Determine OpenCV runtime DLL directory
if(NOT DEFINED OPENCV_BIN_DIR)
    if(EXISTS "${OpenCV_DIR}/bin")
        set(OPENCV_BIN_DIR "${OpenCV_DIR}/bin")
    elseif(DEFINED ENV{OpenCV_DIR} AND EXISTS "$ENV{OpenCV_DIR}/bin")
        set(OPENCV_BIN_DIR "$ENV{OpenCV_DIR}/bin")
    elseif(EXISTS "C:/tools/opencv3411/bin")
        set(OPENCV_BIN_DIR "C:/tools/opencv3411/bin")
    endif()
endif()

# Helper function to copy runtime DLLs (OpenCV, FJFX, etc.)
function(kipsma_copy_runtime_dlls target)
    if(WIN32)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_RUNTIME_DLLS:${target}> $<TARGET_FILE_DIR:${target}>
                COMMAND_EXPAND_LISTS)
        endif()
        if(EXISTS "${FINGERJET_ROOT}/dist/win64/Release/FJFX.dll")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${FINGERJET_ROOT}/dist/win64/Release/FJFX.dll"
                        $<TARGET_FILE_DIR:${target}>
            )
        endif()
        if(EXISTS "${OPENCV_BIN_DIR}")
            file(GLOB OPENCV_DLLS "${OPENCV_BIN_DIR}/*.dll")
            if(OPENCV_DLLS)
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            ${OPENCV_DLLS}
                            $<TARGET_FILE_DIR:${target}>
                )
            endif()
        endif()
    endif()
endfunction()

# ----------------------------------------------------------------- CLI Target
if(KIPSMA_FP_BUILD_CLI)
    add_executable(fingerprint_cli cli/main.cpp)
    target_link_libraries(fingerprint_cli PRIVATE kipsma_fingerprint)
    kipsma_copy_runtime_dlls(fingerprint_cli)
endif()

# ---------------------------------------------------------------- Test Target
if(KIPSMA_FP_BUILD_TESTS)
    enable_testing()

    if(NOT DEFINED DATASET_DIR)
        if(DEFINED ENV{DATASET_DIR})
            set(DATASET_DIR "$ENV{DATASET_DIR}")
        else()
            set(DATASET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../FingerprintMatcher-Data" CACHE PATH "Path to evaluation dataset directory")
        endif()
    endif()

    add_executable(test_fingerprint tests/smoke_test.cpp)
    target_link_libraries(test_fingerprint PRIVATE kipsma_fingerprint)
    kipsma_copy_runtime_dlls(test_fingerprint)
    add_test(NAME smoke_test COMMAND test_fingerprint)

    add_executable(test_enhancer tests/enhancer_test.cpp)
    target_link_libraries(test_enhancer PRIVATE kipsma_fingerprint)
    target_compile_definitions(test_enhancer PRIVATE
        DATASET_DIR="${DATASET_DIR}"
    )
    kipsma_copy_runtime_dlls(test_enhancer)
    add_test(NAME enhancer_test COMMAND test_enhancer)

    add_executable(test_extractor tests/extractor_test.cpp)
    target_link_libraries(test_extractor PRIVATE kipsma_fingerprint)
    target_compile_definitions(test_extractor PRIVATE
        DATASET_DIR="${DATASET_DIR}"
    )
    kipsma_copy_runtime_dlls(test_extractor)
    add_test(NAME extractor_test COMMAND test_extractor)

    add_executable(test_matcher tests/matcher_test.cpp)
    target_link_libraries(test_matcher PRIVATE kipsma_fingerprint)
    target_compile_definitions(test_matcher PRIVATE
        DATASET_DIR="${DATASET_DIR}"
    )
    kipsma_copy_runtime_dlls(test_matcher)
    add_test(NAME matcher_test COMMAND test_matcher)

    add_executable(test_triplet tests/triplet_test.cpp)
    target_link_libraries(test_triplet PRIVATE kipsma_fingerprint)
    kipsma_copy_runtime_dlls(test_triplet)
    add_test(NAME triplet_test COMMAND test_triplet)

    if(WIN32)
        set_tests_properties(smoke_test enhancer_test extractor_test matcher_test triplet_test PROPERTIES
            ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/bin/$<CONFIG>;${FINGERJET_ROOT}/dist/win64/Release;${OPENCV_BIN_DIR};$ENV{PATH}")
    endif()
endif()

# ---------------------------------------------------------------- Install
install(TARGETS kipsma_fingerprint
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
install(DIRECTORY include/kipsma_fingerprint DESTINATION include)

