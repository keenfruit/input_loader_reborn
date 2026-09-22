cmake_minimum_required(VERSION 3.23)
project(InputLoaderReborn)

set(CMAKE_CXX_STANDARD 20)

# Pull in RED4ext SDK
add_subdirectory(deps/red4ext.sdk)

# Automatically download and link pugixml
include(FetchContent)
FetchContent_Declare(
    pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG v1.14
)
FetchContent_MakeAvailable(pugixml)

# Define our plugin library
add_library(${PROJECT_NAME} SHARED src/Main.cpp)

# Link both the RED4ext SDK and pugixml
target_link_libraries(${PROJECT_NAME} PRIVATE RED4ext::SDK pugixml)