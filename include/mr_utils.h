
#include <bits/stdint-uintn.h>
namespace mralloc {

struct GlobalConfig {
    uint16_t node_id;
    uint16_t rdma_cm_port[16];
    uint32_t memory_node_num; 
    char     memory_ips[16][16];

};

int load_config(const char* fname, struct GlobalConfig* config);

}