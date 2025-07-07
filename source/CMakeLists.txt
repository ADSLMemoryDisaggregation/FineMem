include_directories(${PROJECT_SOURCE_DIR}/include)

set(BASE_SOURCE
    rdma_conn_manager.cc rdma_conn.cc free_block_manager.cc memory_node.cc computing_node.cc legoalloc.cc mr_utils.cc
    )

add_library(mralloc STATIC ${BASE_SOURCE})

add_executable(server server.cc)

add_executable(client client.cc)

# add_executable(remote_heap remote_heap.cc)

# message("Build Type: ${CMAKE_BUILD_TYPE} ${CMAKE_CXX_FLAGS}")

target_link_libraries(server
    mralloc
    ibverbs
    rdmacm
    rt
    memkind
)

target_link_libraries(client
    mralloc
    ibverbs
    rdmacm
    rt
    memkind
)

install(TARGETS mralloc
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
        PUBLIC_HEADER DESTINATION include)
