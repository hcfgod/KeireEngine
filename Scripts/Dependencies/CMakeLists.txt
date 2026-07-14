cmake_minimum_required(VERSION 3.20)
project(KeireDependencies LANGUAGES C CXX)

if(NOT DEFINED KEIRE_SDL_SOURCE OR NOT EXISTS "${KEIRE_SDL_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "KEIRE_SDL_SOURCE must point at the locked SDL source tree")
endif()

add_subdirectory("${KEIRE_SDL_SOURCE}" SDL)

add_library(KeireSDLDependency INTERFACE)
target_link_libraries(KeireSDLDependency INTERFACE SDL3::SDL3-static)

add_custom_target(keire-dependencies ALL)
add_dependencies(keire-dependencies SDL3-static)
