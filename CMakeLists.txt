cmake_minimum_required(VERSION 3.24)
project(QuickQRNative VERSION 1.1.0 LANGUAGES CXX)

include(FetchContent)

if(MSVC)
  # Statically link the MSVC C/C++ runtime so QuickQR needs no VC++ redistributable.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(ZXING_READERS ON CACHE BOOL "" FORCE)
set(ZXING_WRITERS OFF CACHE STRING "" FORCE)
set(ZXING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZXING_BLACKBOX_TESTS OFF CACHE BOOL "" FORCE)
set(ZXING_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(ZXING_PYTHON_MODULE OFF CACHE BOOL "" FORCE)
set(ZXING_C_API OFF CACHE BOOL "" FORCE)
set(ZXING_LINK_CPP_STATICALLY ON CACHE BOOL "" FORCE)

FetchContent_Declare(
  zxing
  GIT_REPOSITORY https://github.com/zxing-cpp/zxing-cpp.git
  GIT_TAG v2.3.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(zxing)

add_executable(QuickQR WIN32 native/QuickQR.cpp)
target_compile_features(QuickQR PRIVATE cxx_std_17)
target_compile_definitions(QuickQR PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(QuickQR PRIVATE
  ZXing::ZXing
  windowscodecs
  mfplat
  mfreadwrite
  mfuuid
  ole32
  comdlg32
  shell32
  user32
  gdi32
)

if(MSVC)
  target_compile_options(QuickQR PRIVATE /W4 /permissive- /utf-8 /EHsc)
  target_link_options(QuickQR PRIVATE /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS)
endif()

set_target_properties(QuickQR PROPERTIES
  OUTPUT_NAME "QuickQR"
  WIN32_EXECUTABLE TRUE
)
