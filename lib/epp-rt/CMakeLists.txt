
if(RT32 OR CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_library(epp-rt SHARED
        Runtime32.cpp
    )
    set_target_properties(epp-rt PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
else()
    add_library(epp-rt SHARED
        Runtime64.cpp
    )
endif()


