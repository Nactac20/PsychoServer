cmake_minimum_required(VERSION 3.16)
project(PsychoServer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SQLITE3_DIR "${CMAKE_SOURCE_DIR}/external/sqlite3")
set(SQLITE3_INCLUDE_DIR "${SQLITE3_DIR}/include")
set(SQLITE3_LIBRARY "${SQLITE3_DIR}/lib/sqlite3.lib")
set(SQLITE3_DLL "${SQLITE3_DIR}/lib/sqlite3.dll")

if(NOT EXISTS ${SQLITE3_INCLUDE_DIR}/sqlite3.h)
    message(FATAL_ERROR "sqlite3.h not found in ${SQLITE3_INCLUDE_DIR}")
endif()

if(NOT EXISTS ${SQLITE3_LIBRARY})
    message(FATAL_ERROR "sqlite3.lib not found in ${SQLITE3_DIR}/lib")
endif()

set(SERVER_SOURCES
    main.cpp
    src/database/Database.cpp
)

add_executable(psycho_demo ${SERVER_SOURCES})

target_include_directories(psycho_demo PRIVATE 
    src
    ${SQLITE3_INCLUDE_DIR}
)

target_link_libraries(psycho_demo PRIVATE ${SQLITE3_LIBRARY})

add_custom_command(TARGET psycho_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SQLITE3_DLL}
        $<TARGET_FILE_DIR:psycho_demo>
)

enable_testing()
add_subdirectory(tests)