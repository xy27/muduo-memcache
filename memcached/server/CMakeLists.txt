if(BOOSTPO_LIBRARY)
  add_executable(memcached Item.cc MemcacheServer.cc Session.cc server.cc)
  target_link_libraries(memcached muduo_net muduo_inspect muduo_http muduo_base boost_program_options pthread)
endif()

add_executable(memcached_footprint Item.cc MemcacheServer.cc Session.cc footprint_test.cc)
target_link_libraries(memcached_footprint muduo_net muduo_inspect muduo_http muduo_base pthread)
