set(BASE_INCLUDE
    msg.h rdma_conn.h rdma_conn_manager.h free_block_manager.h memory_heap.h rpc_server.h cpu_cache.h memory_node.h computing_node.h legoalloc.h mr_utils.h)

install(FILES   ${BASE_INCLUDE}
    DESTINATION include)