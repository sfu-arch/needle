
if(RT32 OR CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_definitions(-DRT32)
endif()

add_library(epp-rt SHARED
    Runtime.cpp
)

if(RT32 OR CMAKE_SIZEOF_VOID_P EQUAL 4)
    set_target_properties(epp-rt PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
endif()


