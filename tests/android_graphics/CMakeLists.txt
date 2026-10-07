cmake_minimum_required(VERSION 3.22)
project(NFSU2GraphicsMemoryCheck CXX)
get_filename_component(NFS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(graphics_memory_check resource_lock.cpp)
target_compile_features(graphics_memory_check PRIVATE cxx_std_17)
target_include_directories(graphics_memory_check SYSTEM PRIVATE "${NFS_ROOT}/build/dxvk-source/include/native/windows"
  "${NFS_ROOT}/build/dxvk-source/include/native/directx")
target_compile_options(graphics_memory_check PRIVATE -Wall -Wextra -Werror)
