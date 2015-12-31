add_library(epp-rt SHARED
    Runtime.cpp
    )

llvm_map_components_to_libnames(REQ_LLVM_LIBRARIES ${LLVM_TARGETS_TO_BUILD} support)

target_link_libraries(epp-rt ${REQ_LLVM_LIBRARIES})
