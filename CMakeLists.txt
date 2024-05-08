cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 17)

project(SamCPP)

find_package(OpenCV CONFIG REQUIRED)

# find_package(gflags CONFIG REQUIRED)
add_library(sam_cpp_lib SHARED sam.h sam.cpp)

if(WIN32)
  set(onnxruntime_lib D:/source_package_code/onnxruntime-win-x64-1.17.1/lib/onnxruntime.lib)
  target_include_directories(sam_cpp_lib PRIVATE D:/source_package_code/onnxruntime-win-x64-1.17.1/include)
else()
  set(onnxruntime_lib ${ONNXRUNTIME_ROOT_DIR}/lib/libonnxruntime.so)
  target_include_directories(sam_cpp_lib PRIVATE ${ONNXRUNTIME_ROOT_DIR}/include)
endif()

target_link_libraries(sam_cpp_lib PRIVATE
  ${onnxruntime_lib}
  ${OpenCV_LIBS}
)

add_executable(sam_cpp_test test.cpp)
target_include_directories(sam_cpp_test PRIVATE "D:/source_package_code/gflags-2.2.2/build/include")
target_link_directories(sam_cpp_test PRIVATE "D:/source_package_code/gflags-2.2.2/build/lib/Debug")

# 链接 gflags 库
target_link_libraries(sam_cpp_test PRIVATE
  sam_cpp_lib
  ${OpenCV_LIBS}

  gflags_nothreads_static_debug
  gflags_static_debug

  shlwapi
)
