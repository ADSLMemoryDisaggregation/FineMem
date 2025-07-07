
#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include <fstream>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/foreach.hpp> 
#include "mr_utils.h"

namespace mralloc {

int load_config(const char* fname, struct GlobalConfig* config) {
    std::fstream config_fs(fname);

    boost::property_tree::ptree pt;
    try {
        boost::property_tree::read_json(config_fs, pt);
    } catch (boost::property_tree::ptree_error & e) {
        return -1;
    }

    try {
        // config->server_id = pt.get<uint32_t>("server_id");
        config->node_id = pt.get<uint16_t>("node_id");
        //   = pt.get<uint16_t>("rdma_cm_port");
        config->memory_node_num = pt.get<uint16_t>("memory_node_num");

        int i = 0;

        BOOST_FOREACH(boost::property_tree::ptree::value_type & v, pt.get_child("rdma_cm_port")) {
            config->rdma_cm_port[i] = v.second.get<uint16_t>("");
            // strcpy(config->memory_ips[i], ip.c_str());
            i ++;
        }

        i = 0;

        BOOST_FOREACH(boost::property_tree::ptree::value_type & v, pt.get_child("memory_ips")) {
            std::string ip = v.second.get<std::string>("");
            strcpy(config->memory_ips[i], ip.c_str());
            i ++;
        }
        
        // std::string server_base_addr_str = pt.get<std::string>("mem_pool_base_addr");
        // sscanf(server_base_addr_str.c_str(), "0x%lx", &config->mem_pool_base_addr);
        // config->mem_pool_size   = pt.get<uint64_t>("mem_pool_size");
        // config->block_size        = pt.get<uint64_t>("block_size");
    } catch (boost::property_tree::ptree_error & e) {
        return -1;
    }
    return 0;
}

}