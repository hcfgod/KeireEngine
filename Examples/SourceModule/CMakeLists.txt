cmake_minimum_required(VERSION 3.20)
project(KeireSourceModuleConsumer LANGUAGES CXX)

find_package(Keire CONFIG REQUIRED)

keire_define_source_module_pack(GameplayModules
    SOURCES
        GameplayModule.cpp
    INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(SourceModuleConsumer ClientApplication.cpp)
target_link_libraries(SourceModuleConsumer PRIVATE Keire::Core)
keire_link_source_module_pack(SourceModuleConsumer GameplayModules)
