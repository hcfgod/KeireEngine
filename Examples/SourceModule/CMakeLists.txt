cmake_minimum_required(VERSION 3.20)
project(KeireSourceModuleConsumer LANGUAGES CXX)

find_package(Keire CONFIG REQUIRED)

keire_define_source_module_pack(GameplayModules
    SOURCES
        Source/GameplayModule.cpp
    INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_SOURCE_DIR}/Include
)

add_executable(SourceModuleConsumer Source/ClientApplication.cpp)
target_include_directories(SourceModuleConsumer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Include)
target_link_libraries(SourceModuleConsumer PRIVATE Keire::Core)
keire_link_source_module_pack(SourceModuleConsumer GameplayModules)
