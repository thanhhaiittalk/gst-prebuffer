cmake_minimum_required(VERSION 3.10)
project(gst_prebuffer C)

# Find GStreamer
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-base-1.0)

# Plugin: libgstprebuffer.so
add_library(gstprebuffer SHARED
    src/gstprebuffer.c
)

target_include_directories(gstprebuffer PRIVATE
    ${GST_INCLUDE_DIRS}
)

target_link_libraries(gstprebuffer
    ${GST_LIBRARIES}
)

target_compile_options(gstprebuffer PRIVATE
    ${GST_CFLAGS_OTHER}
    -fPIC
)

target_compile_definitions(gstprebuffer PRIVATE PACKAGE="prebuffer")

# Install plugin to ~/.local/lib/gstreamer-1.0
set(PLUGIN_INSTALL_DIR "$ENV{HOME}/.local/lib/gstreamer-1.0")
install(TARGETS gstprebuffer LIBRARY DESTINATION ${PLUGIN_INSTALL_DIR})

# Example app (empty)
add_executable(prebuffer_demo
    example/prebuffer_demo.c
)

target_include_directories(prebuffer_demo PRIVATE
    ${GST_INCLUDE_DIRS}
)

target_link_libraries(prebuffer_demo
    ${GST_LIBRARIES}
)
