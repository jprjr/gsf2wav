cmake_minimum_required(VERSION 3.1)

project(gsf2wav)

find_package(ZLIB REQUIRED)

add_subdirectory(lazygsf)
add_library(psflib STATIC psflib/psflib.c psflib/psf2fs.c)
target_link_libraries(psflib PRIVATE ZLIB::ZLIB)

add_executable(gsf2wav gsf2wav.c)
target_include_directories(gsf2wav PRIVATE psflib)
target_link_libraries(gsf2wav PRIVATE lazygsf psflib)
