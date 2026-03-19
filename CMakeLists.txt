cmake_minimum_required(VERSION 3.16)
project(PsychoServer VERSION 1.0.0 LANGUAGES CXX)

cmake_policy(SET CMP0167 NEW)
cmake_policy(SET CMP0135 NEW)

if(WIN32)
    add_compile_definitions(
        _WIN32_WINNT=0x0601
        WIN32_LEAN_AND_MEAN
        BOOST_JSON_DYN_LINK=1
        BOOST_ALL_DYN_LINK=1
        BOOST_JSON_STATIC_LINK=0
    )
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED COMPONENTS system thread json)

set(SQLITE3_DIR "${CMAKE_SOURCE_DIR}/external/sqlite3")
set(SQLITE3_INCLUDE_DIR "${SQLITE3_DIR}/include")
set(SQLITE3_LIBRARY "${SQLITE3_DIR}/lib/sqlite3.lib")
set(SQLITE3_DLL "${SQLITE3_DIR}/lib/sqlite3.dll")

if(NOT EXISTS ${SQLITE3_INCLUDE_DIR}/sqlite3.h)
    message(FATAL_ERROR "sqlite3.h not found")
endif()

add_library(psycho_logic STATIC
    src/database/Database.cpp
)

target_include_directories(psycho_logic PUBLIC 
    src
    ${SQLITE3_INCLUDE_DIR}
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(psycho_logic PUBLIC ${SQLITE3_LIBRARY})

add_executable(psycho_server
    src/network/Server.cpp
    src/network/NetworkSession.cpp   
    src/network/RequestHandler.cpp
    main.cpp
)

target_include_directories(psycho_server PRIVATE 
    src
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(psycho_server PRIVATE 
    psycho_logic
    Boost::system
    Boost::thread
    Boost::json                 
)

add_custom_command(TARGET psycho_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SQLITE3_DLL}
        $<TARGET_FILE_DIR:psycho_server>
)

enable_testing()
add_subdirectory(tests)