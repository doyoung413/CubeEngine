# common_vk
cmake_minimum_required(VERSION 3.20)

set(VK_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/source")
set(VK_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs")

FILE(GLOB VK_SOURCES "${VK_SRC_DIR}/*.cpp")
FILE(GLOB VK_INCLUDES "${VK_INC_DIR}/*.hpp")

if (MSVC)
    add_compile_options(/W4)
    add_compile_options(/MP)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

add_library(common_vk STATIC ${VK_SOURCES} ${VK_INCLUDES})

target_include_directories(common_vk PRIVATE ${VK_INC_DIR})
target_include_directories(common_vk PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../libs)
target_include_directories(common_vk PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../libs/SDL3-3.2.16/include)
target_include_directories(common_vk PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../libs/imgui-1.91.9b)
target_include_directories(common_vk PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../libs/NVIDIA_Nsight_Aftermath_SDK_2025.1.0.25009/include)
target_include_directories(common_vk PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include/DebugTools)

find_package(Vulkan REQUIRED)
target_link_libraries(common_vk PRIVATE ${Vulkan_LIBRARIES})
target_include_directories(common_vk PUBLIC ${Vulkan_INCLUDE_DIR})
