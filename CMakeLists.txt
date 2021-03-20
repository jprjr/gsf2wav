cmake_minimum_required(VERSION 3.1)

project(gsf2wav)

add_subdirectory(lazygsf)
add_library(psflib STATIC psflib/psflib.c psflib/psf2fs.c)

find_package(ZLIB REQUIRED)

add_executable(gsf2wav gsf2wav.c)
target_include_directories(gsf2wav PRIVATE psflib ${ZLIB_INCLUDE_DIRS})
target_link_libraries(gsf2wav PRIVATE lazygsf psflib ${ZLIB_LIBRARIES})
