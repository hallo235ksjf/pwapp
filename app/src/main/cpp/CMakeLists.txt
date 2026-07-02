cmake_minimum_required(VERSION 3.22.1)
project(player)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dear ImGui Quellen (liegen in ./imgui) ---
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)

add_library(player SHARED
    main.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_android.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(player PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(glesv3-lib GLESv3)
find_library(mediandk-lib mediandk)
find_library(aaudio-lib aaudio)

# android_native_app_glue kommt aus dem NDK
add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)
target_include_directories(native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)

target_link_libraries(player
    native_app_glue
    ${log-lib}
    ${android-lib}
    ${egl-lib}
    ${glesv3-lib}
    ${mediandk-lib}
    ${aaudio-lib}
)

# Damit NativeActivity die App findet
set_target_properties(player PROPERTIES LINK_FLAGS "-u ANativeActivity_onCreate")
