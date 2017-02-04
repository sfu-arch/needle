
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/workloads DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Common.mk.in" 
            "${CMAKE_CURRENT_BINARY_DIR}/workloads/Common.mk" @ONLY)

