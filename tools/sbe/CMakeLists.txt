set(LLVM_USED_LIBS sb)

#configure_file("${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake" 
               #"${CMAKE_CURRENT_BINARY_DIR}/config.h" @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(sbe
  main.cpp
  )

llvm_map_components_to_libnames(REQ_LLVM_LIBRARIES ${LLVM_TARGETS_TO_BUILD}
        asmparser core linker bitreader bitwriter irreader ipo scalaropts
        analysis target mc support)

    target_link_libraries(sbe inliner namer sb common simplify InstruMem stats ${REQ_LLVM_LIBRARIES} ${Boost_LIBRARIES})

# Platform dependencies.
target_link_libraries(sbe
pthread
dl
curses
)

set_target_properties(sbe
                      PROPERTIES
                      LINKER_LANGUAGE CXX
                      PREFIX "")

install(TARGETS sbe 
  RUNTIME DESTINATION bin)

