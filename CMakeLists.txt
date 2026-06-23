cmake_minimum_required(VERSION 3.25)

project(DASWxHost LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(WX_ROOT "C:/wxWidgets-3.3.2" CACHE PATH "wxWidgets root directory")
set(wxWidgets_ROOT_DIR "${WX_ROOT}" CACHE PATH "wxWidgets root directory")
set(wxWidgets_LIB_DIR "${WX_ROOT}/lib/vc_x64_lib" CACHE PATH "wxWidgets MSVC x64 lib directory")
set(wxWidgets_CONFIGURATION mswu CACHE STRING "wxWidgets configuration")
set(wxWidgets_USE_STATIC ON CACHE BOOL "Use wxWidgets static libraries")

find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

include(CheckLanguage)
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(DAS_HAS_CUDA ON)
else()
    set(DAS_HAS_CUDA OFF)
endif()

set(DAS_SOURCES
    app.rc
    src/main.cpp
    src/AudioFileDecoder.cpp
    src/AudioStreamPlayer.cpp
    src/MainFrame.cpp
    src/PlotPanel.cpp
    src/DasSimulator.cpp
    src/DasDemodulator.cpp
    src/CudaProcessor.cpp
    src/NiDmaDecoder.cpp
)

add_executable(das_wx_host WIN32 ${DAS_SOURCES})
target_include_directories(das_wx_host PRIVATE src)
target_link_libraries(das_wx_host PRIVATE ${wxWidgets_LIBRARIES})
if(WIN32)
    target_link_libraries(das_wx_host PRIVATE mfplat mfreadwrite mfuuid winmm)
endif()
target_compile_definitions(das_wx_host PRIVATE
    $<$<BOOL:${DAS_HAS_CUDA}>:DAS_WITH_CUDA=1>
    $<$<NOT:$<BOOL:${DAS_HAS_CUDA}>>:DAS_WITH_CUDA=0>
)

if(MSVC)
    target_compile_options(das_wx_host PRIVATE /W4 /permissive- /utf-8)
endif()

if(DAS_HAS_CUDA)
    add_library(das_cuda STATIC src/CudaKernels.cu)
    target_include_directories(das_cuda PRIVATE src)
    set_target_properties(das_cuda PROPERTIES
        CUDA_STANDARD 17
        CUDA_STANDARD_REQUIRED ON
        CUDA_SEPARABLE_COMPILATION OFF
    )
    target_link_libraries(das_wx_host PRIVATE das_cuda)
    target_compile_definitions(das_cuda PRIVATE DAS_WITH_CUDA=1)
endif()

message(STATUS "wxWidgets root: ${WX_ROOT}")
message(STATUS "CUDA enabled: ${DAS_HAS_CUDA}")
