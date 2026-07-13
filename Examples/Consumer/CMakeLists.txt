cmake_minimum_required(VERSION 3.20)
project(CoreSdkConsumer LANGUAGES CXX)

find_package(CrossPlatformCoreClientTemplate CONFIG REQUIRED)

add_executable(SdkConsumer Main.cpp)
target_link_libraries(SdkConsumer PRIVATE Core::Core)
