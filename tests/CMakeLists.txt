add_executable(test_api test_api.c)
target_link_libraries(test_api PRIVATE couchdb_sync_c)
add_test(NAME api COMMAND test_api)
