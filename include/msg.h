
#pragma once

#include <assert.h>
#include <bits/stdint-uintn.h>
#include <stdint.h>
#include <chrono>
#include <pthread.h>

namespace mralloc {

#define NOTIFY_WORK 0xFF
#define NOTIFY_IDLE 0x00
#define MAX_MSG_SIZE 512
#define MAX_SERVER_WORKER 1
#define MAX_SERVER_CLIENT 4096
#define RESOLVE_TIMEOUT_MS 5000
#define RDMA_TIMEOUT_US (uint64_t)100000000  // 10s
#define MAX_REMOTE_SIZE (1UL << 25)

#define TIME_NOW (std::chrono::high_resolution_clock::now())
#define TIME_DURATION_US(START, END)                                      \
  (std::chrono::duration_cast<std::chrono::microseconds>((END) - (START)) \
       .count())

enum MsgType { MSG_REGISTER, MSG_UNREGISTER, MSG_FETCH, MSG_FETCH_FAST, MSG_MW_BIND, RPC_FUSEE_SUBTABLE, MSG_MW_REBIND, MSG_MW_CLASS_BIND, MSG_FREE_FAST, MSG_PRINT_INFO, MSG_MW_BATCH};

enum ResStatus { RES_OK, RES_FAIL };

enum ConnMethod {CONN_RPC, CONN_ONESIDE, CONN_FUSEE};

enum MRType:uint64_t {MR_IDLE, MR_FREE, MR_TRANSFER};

#define CHECK_RDMA_MSG_SIZE(T) \
    static_assert(sizeof(T) < MAX_MSG_SIZE, #T " msg size is too big!")

// buffer size = 8 MiB
struct MsgBuffer {
    MRType msg_type[8];
    void* buffer;
};

struct PublicInfo {
    uint64_t pid_alive[1024];
    uint16_t id_node_map[1024];
    MsgBuffer node_buffer[128];
};

struct CNodeInit {
    uint16_t node_id;
    uint8_t access_type;
};

struct PData {
    uint64_t buf_addr;
    uint32_t buf_rkey;
    uint64_t size;
    uint16_t id;
    uint64_t block_size_;
    uint64_t block_num_;
    uint32_t global_rkey_;
    uint64_t section_header_;
    uint64_t heap_start_;
};

struct CmdMsgBlock {
    uint8_t rsvd1[MAX_MSG_SIZE - 1];
    volatile uint8_t notify;
};

struct CmdMsgRespBlock {
    uint8_t rsvd1[MAX_MSG_SIZE - 1];
    volatile uint8_t notify;
};

class RequestsMsg {
public:
    uint64_t resp_addr;
    uint32_t resp_rkey;
    uint16_t id;
    uint8_t type;
};
CHECK_RDMA_MSG_SIZE(RequestsMsg);

class ResponseMsg {
public:
    uint8_t status;
};
CHECK_RDMA_MSG_SIZE(ResponseMsg);

class FetchBlockResponse : public ResponseMsg {
public:
    uint64_t addr;
    uint32_t rkey;
    uint64_t size;
};

class FetchFastRequest : public RequestsMsg{
public:
    uint16_t size_class;
};
CHECK_RDMA_MSG_SIZE(FetchFastRequest);

class FreeFastRequest : public RequestsMsg{
public:
    uint64_t addr;
};
CHECK_RDMA_MSG_SIZE(FreeFastRequest);

class RebindBlockRequest : public RequestsMsg{
public:
    uint64_t addr;
};
CHECK_RDMA_MSG_SIZE(RebindBlockRequest);

class RebindBlockResponse : public ResponseMsg {
public:
    uint32_t rkey;
};
CHECK_RDMA_MSG_SIZE(RebindBlockResponse);

class InfoResponse : public ResponseMsg {
public:
    uint64_t total_mem;
};
CHECK_RDMA_MSG_SIZE(RebindBlockResponse);

class RebindBatchRequest : public RequestsMsg{
public:
    uint64_t addr[32];
};
CHECK_RDMA_MSG_SIZE(RebindBlockRequest);

class RebindBatchResponse : public ResponseMsg {
public:
    uint32_t rkey[32];
};
CHECK_RDMA_MSG_SIZE(RebindBlockResponse);

class RegisterRequest : public RequestsMsg {
public:
    uint64_t size;
};
CHECK_RDMA_MSG_SIZE(RegisterRequest);

class RegisterResponse : public ResponseMsg {
public:
    uint64_t addr;
    uint32_t rkey;
};
CHECK_RDMA_MSG_SIZE(RegisterResponse);

class MWbindRequest : public RequestsMsg {
public:
    uint64_t newkey;
    uint64_t addr;
    uint32_t rkey;
    uint64_t size;
};
CHECK_RDMA_MSG_SIZE(MWbindRequest);

class MWbindResponse : public ResponseMsg {
public:
    uint64_t addr;
    uint32_t rkey;
    uint64_t size;
};
CHECK_RDMA_MSG_SIZE(MWbindRequest);

class FetchRequest : public RequestsMsg {
public:
    uint64_t size;
};
CHECK_RDMA_MSG_SIZE(MWbindRequest);

class FetchResponse : public ResponseMsg {
public:
    uint64_t addr;
    uint32_t rkey;
    uint64_t size;
};
CHECK_RDMA_MSG_SIZE(MWbindRequest);

class FuseeSubtableResponse : public ResponseMsg {
public:
    uint64_t addr;
    uint32_t rkey;
};

struct UnregisterRequest : public RequestsMsg {
public:
    uint64_t addr;
};
CHECK_RDMA_MSG_SIZE(UnregisterRequest);

struct UnregisterResponse : public ResponseMsg {};
CHECK_RDMA_MSG_SIZE(UnregisterResponse);


}  // namespace kv
