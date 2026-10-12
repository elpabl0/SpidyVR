# The Quest app's native library: the lab and its OpenXR session here, the
# portable core and the city's renderer from the repository's own build.
cmake_minimum_required(VERSION 3.24)
project(spidy_quest LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SPIDY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../..")
set(SPIDY_CORE_ONLY ON CACHE BOOL "" FORCE)
add_subdirectory(${SPIDY_ROOT} spidy)

find_package(OpenXR REQUIRED CONFIG) # the Khronos loader's prefab package (app/build.gradle)
add_library(native_app_glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)

add_library(spidy_quest SHARED quest_main.cpp quest_xr.cpp)
# OpenXR structs are filled as {TYPE}: the rest zero on purpose.
target_compile_options(spidy_quest PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
target_link_libraries(spidy_quest PRIVATE spidy_city_gles native_app_glue OpenXR::openxr_loader android EGL GLESv3
                      log)
# android_main is called by name from the glue: keep it.
target_link_options(spidy_quest PRIVATE "-u" "ANativeActivity_onCreate")
