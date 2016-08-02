add_executable(pasha-pip
    main.cpp  
)

set_target_properties(pasha-pip
                      PROPERTIES
                      LINKER_LANGUAGE CXX
                      PREFIX "")

install(TARGETS pasha-pip
  RUNTIME DESTINATION bin)

