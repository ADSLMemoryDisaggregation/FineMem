
#include <cstdio>
#include <string>
#include <iostream>
#include "memory_node.h"
#include <sys/time.h>

void* run_woker_thread(void* arg){
    mralloc::MemoryNode* heap = (mralloc::MemoryNode*)arg;
    struct timeval st, et;
    gettimeofday(&st, NULL);    
    while(1){
      gettimeofday(&et, NULL);
      if((et.tv_sec - st.tv_sec)*1000000 + et.tv_usec - st.tv_usec > 200000){
        gettimeofday(&st, NULL); 
        heap->print_alloc_info();
        // if((et.tv_sec - st.tv_sec)*1000000 + et.tv_usec - st.tv_usec > 200000) {
        //   printf("too long\n");
        // }
      }
      usleep(1000);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
  if (argc < 4) {
    std::cout << "Usage: " << argv[0] << "<device> <ip> <port>" << std::endl;
    return 0;
  }

  std::string device = argv[1];
  std::string ip = argv[2];
  std::string port = argv[3];
  mralloc::MemoryNode *heap = new mralloc::MemoryNode(true);
  heap->start(ip, port, device);
  pthread_t print_thread;
  pthread_create(&print_thread, NULL, run_woker_thread, heap);
  char s;
  while (s = getchar()) {
    if(s=='r'){
      int id;
      std::cin >> id;
      heap->recovery(id);
    } else if(s=='k'){
      pthread_cancel(print_thread);
    }
    else
      heap->print_alloc_info();
  }

  getchar();
  heap->stop();
  delete heap;
  return 0;
}