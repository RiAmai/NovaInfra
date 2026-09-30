cmake_minimum_required(VERSION 3.21)
project(NovaInfraPackageConsumer LANGUAGES CXX)

find_package(NovaInfra CONFIG REQUIRED)

add_executable(novainfra_package_consumer main.cpp)
target_link_libraries(novainfra_package_consumer PRIVATE NovaInfra::Logger)
