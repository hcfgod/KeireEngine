cmake_minimum_required(VERSION 3.20)
project(ManagedCoreSdkConsumer LANGUAGES CXX)

find_package(Keire CONFIG REQUIRED)

add_executable(ManagedSdkConsumer Source/ClientApplication.cpp)
target_link_libraries(ManagedSdkConsumer PRIVATE Keire::Core)
