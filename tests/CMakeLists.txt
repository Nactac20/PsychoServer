include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)

# Тесты базы данных
add_executable(psycho_tests 
    test_database.cpp 
    ../src/database/Database.cpp
)

target_include_directories(psycho_tests PRIVATE 
    ../src 
    ${SQLITE3_INCLUDE_DIR}
)

target_link_libraries(psycho_tests PRIVATE 
    ${SQLITE3_LIBRARY} 
    GTest::gtest 
    GTest::gtest_main
)

add_executable(network_integration_tests 
    ../tests/test_network_integration.cpp 
    ../src/network/Server.cpp
    ../src/network/NetworkSession.cpp
    ../src/network/RequestHandler.cpp
    ../src/database/Database.cpp
)

target_include_directories(network_integration_tests PRIVATE 
    ../src 
    ${SQLITE3_INCLUDE_DIR}
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(network_integration_tests PRIVATE 
    ${SQLITE3_LIBRARY}
    Boost::system
    Boost::thread
    Boost::json
    GTest::gtest
    GTest::gtest_main
)

add_custom_command(TARGET psycho_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SQLITE3_DLL}
        $<TARGET_FILE_DIR:psycho_tests>
)

add_custom_command(TARGET network_integration_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SQLITE3_DLL}
        $<TARGET_FILE_DIR:network_integration_tests>
)

include(GoogleTest)
gtest_discover_tests(psycho_tests)
gtest_discover_tests(network_integration_tests)