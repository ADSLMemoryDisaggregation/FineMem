#pragma once

#include "rdma_conn_manager.h"
#include <vector>

namespace mralloc{

struct extend_addr{
    uint64_t addr;
    uint32_t rkey;
    uint32_t node;
};

class LegoAlloc{
public:
    LegoAlloc(){

    }

    int read(void *ptr, uint64_t size, extend_addr remote_addr){}
    int write(void *ptr, uint64_t size, extend_addr remote_addr){}
    extend_addr malloc(uint64_t size){
    }

private:
    std::vector<ConnectionManager*> nodes;
    int node_num_ = 0;
    int round_ = 0;
};


}