add_library(epp-rt SHARED
    Runtime.cpp
    )

# Add a 32 bit library as well

add_library(epp-rt-m32 SHARED
    Runtime.cpp
    )
set_target_properties(epp-rt-m32 PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
