cmake_minimum_required(VERSION 3.20)
project(ps5_native_tooling LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ps5-native-tool
    native_app_builder.cpp
    self_container.cpp
    elf_object.cpp
    sce_module_writer.cpp
)

add_executable(libc-builder
    libc_builder.cpp
)
