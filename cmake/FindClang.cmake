#
# Check for Clang.
#
# The following variables are set:
#  CLANG_EXE
#  CLANG_VERSION

find_program(CLANG_EXE NAMES "clang" "clang-3.8" PATHS ${LLVM_INSTALL_PREFIX}/bin NO_DEFAULT_PATH)

if(CLANG_EXE STREQUAL "CLANG_EXE-NOTFOUND")
    set(CLANG_FOUND FALSE)
else()
    set(CLANG_FOUND TRUE)
endif()

message(STATUS ${LLVM_INSTALL_PREFIX})

if(CLANG_FOUND)
    execute_process(COMMAND ${CLANG_EXE} --version OUTPUT_VARIABLE CLANG_VERSION_TEXT OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(SUBSTRING ${CLANG_VERSION_TEXT} 14 5 CLANG_VERSION)
else()
    message(FATAL_ERROR "Clang not found in LLVM installation")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CLANG DEFAULT_MSG
                                  CLANG_VERSION)

mark_as_advanced(CLANG_VERSION)
