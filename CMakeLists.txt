cmake_minimum_required(VERSION 3.10)
project(ddnet_map_loader C)
option(BUILD_SHARED_LIBS "Build as shared library" ON)
find_package(ZLIB REQUIRED)
add_library(ddnet_map_loader
    ddnet_map_loader.c
)
target_include_directories(ddnet_map_loader PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(ddnet_map_loader PRIVATE ZLIB::ZLIB)
set_target_properties(ddnet_map_loader PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)
install(TARGETS ddnet_map_loader
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)
install(FILES ddnet_map_loader.h DESTINATION include)
