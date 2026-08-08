cmake_minimum_required(VERSION 3.20)
project(CoreSdkConsumer LANGUAGES CXX)

find_package(Keire CONFIG REQUIRED)

add_executable(SdkConsumer Source/Main.cpp)
target_link_libraries(SdkConsumer PRIVATE Keire::Core)
