
add_library(epp-inst
    EPPProfile.cpp
    EPPEncode.cpp
    EPPDecode.cpp
    AltCFG.cpp
    )


add_library(epp-rt-rle SHARED
    RuntimeRLE.cpp
)

add_library(epp-rt-agg SHARED
    RuntimeAgg.cpp
)

if(TRACE_RUNTIME)
    message(STATUS "Using RLE Trace Runtime for EPP")
add_custom_command(
    TARGET epp-rt-rle POST_BUILD
    COMMAND ln -sf ${CMAKE_BINARY_DIR}/lib/libepp-rt-rle.so ${CMAKE_BINARY_DIR}/lib/libepp-rt.so
)
else()
add_custom_command(
    TARGET epp-rt-agg POST_BUILD
    COMMAND ln -sf ${CMAKE_BINARY_DIR}/lib/libepp-rt-agg.so ${CMAKE_BINARY_DIR}/lib/libepp-rt.so
)
endif()


