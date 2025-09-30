cmake_minimum_required(VERSION 3.15)
project(ddnet_map_loader C)
option(BUILD_SHARED_LIBS "Build as shared library" ON)
option(DDNET_MAP_LOADER_FETCH_ZLIB "Download zlib if no system zlib is found" ON)
find_package(ZLIB QUIET)

if(NOT ZLIB_FOUND)
    if(NOT DDNET_MAP_LOADER_FETCH_ZLIB)
        message(FATAL_ERROR "Zlib was not found and automatic fetching is disabled. Install zlib or enable DDNET_MAP_LOADER_FETCH_ZLIB.")
    endif()

    include(FetchContent)
    message(STATUS "Zlib not found; fetching v1.3.1 from upstream.")
    FetchContent_Declare(zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
    )
    FetchContent_MakeAvailable(zlib)

    if(NOT TARGET ZLIB::ZLIB)
        if(TARGET zlibstatic)
            add_library(ZLIB::ZLIB ALIAS zlibstatic)
        elseif(TARGET zlib)
            add_library(ZLIB::ZLIB ALIAS zlib)
        else()
            message(FATAL_ERROR "Bundled zlib did not create an expected library target.")
        endif()
    endif()
endif()
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
