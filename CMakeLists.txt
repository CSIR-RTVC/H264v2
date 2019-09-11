cmake_minimum_required(VERSION 3.11)
project(libH264v2 VERSION 1.0.0 LANGUAGES CXX)

#Make sure that custom modules like FindRapidJSON are found
list(INSERT CMAKE_MODULE_PATH 0 ${CMAKE_SOURCE_DIR}/cmake)

include(FetchContent)
FetchContent_Declare(
  Vpp
  GIT_REPOSITORY https://github.com/CSIR-RTVC/vpp
)
# Declare dependencies
find_package(Vpp 1.0.0 REQUIRED)

##############################################
# Create target and set properties

SET(H264_LIB_HDRS
        ./include/H264v2/H264v2.h
        ./include/H264v2Codec/H264v2Codec.h
        ./include/H264v2Codec/H264v2CodecHeader.h
        ./src/stdafx.h
)

SET(H264_LIB_SRCS
	./src/H264v2.cpp
    ./src/H264v2Codec.cpp
    ./src/H264v2CodecHeader.cpp
    ./src/stdafx.h
    ./src/stdafx.cpp
)

ADD_LIBRARY( H264v2 SHARED ${H264_LIB_SRCS} ${H264_LIB_HDRS})

#Add an alias so that library can be used inside the build tree, e.g. when testing
#add_library(H264v2::H264v2 ALIAS H264v2)

#Set target properties
target_include_directories(H264v2
    PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/H264v2>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/H264v2Codec>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(H264v2 PRIVATE cxx_auto_type)
target_compile_options(H264v2 PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wall>)

target_link_libraries(H264v2
    PUBLIC
        Vpp::Vpp
	PRIVATE
)

##############################################
# Installation instructions

include(GNUInstallDirs)
set(INSTALL_CONFIGDIR ${CMAKE_INSTALL_LIBDIR}/cmake/H264v2)

install(TARGETS H264v2
    EXPORT H264v2-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

#Export the targets to a script
install(EXPORT H264v2-targets
  FILE
    H264v2Targets.cmake
  NAMESPACE
    H264v2::
  DESTINATION
    ${INSTALL_CONFIGDIR}
)

#Create a ConfigVersion.cmake file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/H264v2ConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(${CMAKE_CURRENT_LIST_DIR}/cmake/H264v2Config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/H264v2Config.cmake
    INSTALL_DESTINATION ${INSTALL_CONFIGDIR}
)

#Install the config, configversion and custom find modules
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/H264v2Config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/H264v2ConfigVersion.cmake
    DESTINATION ${INSTALL_CONFIGDIR}
)

##############################################
## Exporting from the build tree

export(EXPORT H264v2-targets FILE ${CMAKE_CURRENT_BINARY_DIR}/H264v2Targets.cmake NAMESPACE H264v2::)

#Register package in user's package registry
export(PACKAGE H264v2)

#add_subdirectory(test)
