# CmakeLists.txt in H264v2 dir

IF (WIN32)
    # SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_BUILD_FOR_SHORT")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_CRT_SECURE_NO_WARNINGS")
    # get rid of some compiler warnings
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_CRT_SECURE_NO_WARNINGS")

ELSE (WIN32)
    add_definitions("-Wwrite-strings")
    #SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wwrite-strings")
ENDIF (WIN32)

SET (H264_INCLUDE_DIRS 
	${PROJECT_SOURCE_DIR}/externals/CodecUtils/include/CodecUtils
	${PROJECT_SOURCE_DIR}/externals/GeneralUtils/include/GeneralUtils
	${PROJECT_SOURCE_DIR}/externals/ImageUtils/include/ImageUtils
	${PROJECT_SOURCE_DIR}/externals/StreamUtils/include/StreamUtils
)

#INCLUDE_DIRECTORIES(
#    ${artistCodecIncludes}
#    ${artistUtilIncludes}
#)

## Lib directories
#LINK_DIRECTORIES(
#    ${artistLink}
#)

SET(H264v2_LIB_HDRS
    H264v2.h
    ../includes/H264v2Codec/H264v2Codec.h
    ../includes/H264v2Codec/H264v2CodecHeader.h
    )

SET(H264v2_LIB_SRCS
    H264v2.cpp
    H264v2Codec.cpp
    H264v2CodecHeader.cpp
    stdafx.h
    stdafx.cpp
    )

ADD_LIBRARY(H264v2 SHARED ${H264v2_LIB_SRCS} ${H264v2_LIB_HDRS})
install(TARGETS H264v2
    EXPORT H264v2Targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(EXPORT H264v2Targets
  FILE
    H264v2Targets.cmake
  NAMESPACE
    H264v2::
  DESTINATION
    ${CMAKE_INSTALL_LIBDIR}/cmake/H264v2
)

target_include_directories(H264v2
    PUBLIC 
        $<INSTALL_INTERFACE:>    
        $<BUILD_INTERFACE:${H264_INCLUDE_DIRS}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
		${H264_INCLUDE_DEPENDENCIES}
)

target_compile_features(H264v2 PRIVATE cxx_std_11)


IF (WIN32)
    SET_TARGET_PROPERTIES(H264v2 PROPERTIES COMPILE_FLAGS "/GL")
    SET_TARGET_PROPERTIES(H264v2 PROPERTIES LINK_FLAGS "/LTGC")
ENDIF (WIN32)

TARGET_LINK_LIBRARIES(
    H264v2
#    RtvcCodecUtils
#    RtvcGeneralUtils
#    RtvcImageUtils
#    RtvcStreamUtils
) 
