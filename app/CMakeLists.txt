# find_package(baseclasses 1.0 REQUIRED)

SET(SRCS 
    main.cpp
)

ADD_EXECUTABLE(TestApp 
	${SRCS}
)

target_link_libraries(TestApp)

 