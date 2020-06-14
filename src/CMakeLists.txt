cmake_minimum_required (VERSION 2.8)

list (APPEND NOMAIN_SOURCES
    "strace.cpp"
    "utils.cpp"
    "error_prints.c"
    "SpookyV2.cpp"
)
add_executable ("cache-dash-h" main.cpp ${NOMAIN_SOURCES})
set_target_properties ("cache-dash-h" PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

target_link_libraries("cache-dash-h" SQLiteCpp)

install (TARGETS "cache-dash-h" RUNTIME DESTINATION bin)
install (DIRECTORY . DESTINATION "include/${CMAKE_PROJECT_NAME}"
         FILES_MATCHING PATTERN "*.h")
