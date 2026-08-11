cmake_minimum_required(VERSION 3.21)
project(NovaCorePackageConsumer LANGUAGES CXX)

find_package(NovaCore CONFIG REQUIRED)

add_executable(novacore_package_consumer main.cpp)
target_link_libraries(novacore_package_consumer PRIVATE Nova::Logger)
