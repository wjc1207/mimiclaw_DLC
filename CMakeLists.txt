# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Add the embedded MicroPython as a component
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mimiclaw)

# Suppress the duplicate __assert_func in micropython_embed's embed_util.c.
# That file guards its own __assert_func with #ifndef NDEBUG, but ESP-IDF
# newlib already provides the symbol, causing a "multiple definition" linker
# error.  NDEBUG is safe here: MicroPython uses its own nlr-based exception
# mechanism rather than standard C assert().
target_compile_definitions(__idf_micropython_embed PRIVATE NDEBUG)

# Build the initial SPIFFS image from spiffs_data/.
# Do NOT use FLASH_IN_PROJECT here — that would overwrite the SPIFFS partition
# (and erase long-term memory) every time the firmware is flashed.
# To initialize a fresh device, flash the SPIFFS image once manually:
#   idf.py spiffs-flash
spiffs_create_partition_image(spiffs spiffs_data)
