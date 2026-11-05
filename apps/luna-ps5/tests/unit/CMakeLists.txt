# CMake configuration for unit tests
# Observer Pattern Tests

find_package(Threads REQUIRED)

# The test links against the same core library as the application so it can
# never drift from the real implementation (previous versions re-listed a
# subset of sources and failed with unresolved symbols).
add_executable(observer_tests test_observer_pattern.cpp)

target_link_libraries(observer_tests PRIVATE
    luna_core
    Threads::Threads
)

if(MSVC)
    target_compile_options(observer_tests PRIVATE /utf-8)
endif()

add_test(NAME luna_observer_tests COMMAND observer_tests)
