include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)

set(TEST_SOURCES
    test_database.cpp
    ../src/database/Database.cpp
)

add_executable(psycho_tests ${TEST_SOURCES})

target_include_directories(psycho_tests PRIVATE 
    ../src
    ${SQLITE3_INCLUDE_DIR}
)

target_link_libraries(psycho_tests
    PRIVATE
    ${SQLITE3_LIBRARY}
    GTest::gtest
    GTest::gtest_main
)

add_custom_command(TARGET psycho_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SQLITE3_DLL}
        $<TARGET_FILE_DIR:psycho_tests>
)

include(GoogleTest)
gtest_discover_tests(psycho_tests)