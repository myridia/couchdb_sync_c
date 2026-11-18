cmake_minimum_required(VERSION 3.16)
project(couchdb_sync_c C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(couchdb_sync_c
    src/couchdb_sync.c
    src/http_curl.c
    src/store_sqlite.c
    src/sync_pull.c
    src/sync_push.c
    src/checkpointer.c
    src/replication_id.c
    src/rev.c
    src/util.c
)

target_include_directories(couchdb_sync_c
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(CURL)
find_package(SQLite3)

if(CURL_FOUND)
    target_link_libraries(couchdb_sync_c PUBLIC CURL::libcurl)
endif()

if(SQLite3_FOUND)
    target_link_libraries(couchdb_sync_c PUBLIC SQLite::SQLite3)
endif()

add_executable(couchdb_sync_example examples/pull_once.c)
target_link_libraries(couchdb_sync_example PRIVATE couchdb_sync_c)
