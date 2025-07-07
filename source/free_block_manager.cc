
#include "free_block_manager.h"
#include <bits/stdint-uintn.h>
#include <sys/types.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>

namespace mralloc {
    
    const int retry_threshold = 3;
    const int low_threshold = 2;

    void ServerBlockManager::recovery(int node){
        if(node == 0) {
            section_e init_section_header = {0, 0, 0, 0, 0, 1, 0, 0};
            region_e init_region_header = {0, 0, 0, 0, 0, 1, 0, 0};

            for(int i = 0; i < section_num_; i++) {
                section_header_[i].store(init_section_header);
            }
            
            for(int i = 0; i < region_num_; i++) {
                region_header_[i].store(init_region_header);
            }
            block_e blank = {0,0};
            for(uint64_t i = 0; i < block_num_; i++) {
                block_rkey_[i].store({0,0});
                block_header_[i].store(blank);
                memset(&block_header_[i], 0, sizeof(uint64_t));
            }
            return;
        }
        int counter = 0;
        for(int i = 0; i < region_num_; i++) {
            uint64_t flush_map = 0;
            region_e region = region_header_[i].load();
            region_e new_region; 
            do{
                new_region = region;
                if(region.last_modify_id_ == node){
                    flush_map |= (uint64_t)1 << region.last_offset_;
                    new_region.last_modify_id_ = 0;
                }
                new_region.base_map_ &= ~(flush_map);
                if(flush_map == 0 || *(uint64_t*)&new_region == *(uint64_t*)&region){
                    counter --;
                    break;
                }
                if(new_region.base_map_ == 0) {
                    new_region.on_use_ = 0;
                }    
            }while(!region_header_[i/block_per_region].compare_exchange_weak(region, new_region));
            counter++;
            if (new_region.base_map_ == 0){
                section_e sect = section_header_[i/block_per_region/region_per_section].load();
                section_e new_sect;
                do{
                    int offset = i/block_per_region%region_per_section;
                    new_sect = sect;
                    new_sect.alloc_map_ &= ~((uint16_t)1<<(offset));
                    new_sect.frag_map_ &= ~((uint16_t)1<<(offset));
                }while(!section_header_[i/block_per_region/region_per_section].compare_exchange_weak(sect, new_sect));
            }
        } 
        printf("region scan success, scan block log...\n");
        block_e blank = {0,0};
        for(int i=0; i < block_num_; i++){
            block_e log = block_header_[i].load();
            blank.timestamp_ = (log.timestamp_+1)%128;
            block_header_[i].store(blank);
            if(log.client_id_ == node){
                // do recovery
                uint64_t flush_map = 0;
                region_e region = region_header_[i/block_per_region].load();
                region_e new_region; 
                do{
                    new_region = region;
                    // if(region.last_modify_id_ == node){
                    //     flush_map |= (uint64_t)1 << region.last_offset_;
                    //     new_region.last_modify_id_ = 0;
                    //     new_region.last_offset_ = 0;
                    //     new_region.last_timestamp_ = (new_region.last_timestamp_ + 1) % 128;
                    // }
                    flush_map |= (uint64_t)1 << i%block_per_region;
                    new_region.base_map_ &= ~(flush_map);
                    if(*(uint64_t*)&new_region == *(uint64_t*)&region){
                        counter --;
                        break;
                    }    
                    if(new_region.base_map_ == 0) {
                        new_region.on_use_ = 0;
                    }    
                }while(!region_header_[i/block_per_region].compare_exchange_weak(region, new_region));
                counter ++;
                if (new_region.base_map_ == 0){
                    section_e sect = section_header_[i/block_per_region/region_per_section].load();
                    section_e new_sect;
                    do{
                        int offset = i/block_per_region%region_per_section;
                        new_sect = sect;
                        new_sect.alloc_map_ &= ~((uint32_t)1<<(offset));
                        new_sect.frag_map_ &= ~((uint32_t)1<<(offset));
                    }while(!section_header_[i/block_per_region/region_per_section].compare_exchange_weak(sect, new_sect));
                }
            }
        }
        printf("recycle total region %d \n", counter);
    }


    bool ServerBlockManager::init(uint64_t meta_addr, uint64_t addr, uint64_t size, uint32_t rkey) {
        assert(size%region_size_ == 0);

        block_num_ = size/block_size_;
        region_num_ = block_num_/block_per_region;
        section_num_ = region_num_/region_per_section;
        global_rkey_ = rkey;

        section_header_ = (section*)meta_addr;
        region_header_ = (region*)(section_header_ + section_num_);
        block_rkey_ = (rkey_table*)(region_header_ + region_num_);
        block_header_ = (block*)(block_rkey_ + block_num_);
        public_info_ = (PublicInfo*)(block_header_ + block_num_);
        for(int i  = 0; i < 128; i++) {
            for(int j = 0; j < 8; j++){
                public_info_->node_buffer[i].msg_type[j] = MRType::MR_IDLE;
            }
            public_info_->node_buffer[i].buffer = (void*)((uint64_t)public_info_ + sizeof(PublicInfo) + (uint64_t)1024*1024*8*i);
        }
        for(int i = 0;i < 1024;i++){
            public_info_->pid_alive[i] = 0;
        }
        
        heap_start_ = addr;
        heap_size_ = size;
        assert(heap_start_ > (uint64_t)((uint64_t)public_info_ + sizeof(PublicInfo) + (uint64_t)1024*1024*8*128));

        if((uint64_t)((uint64_t)public_info_ + sizeof(PublicInfo) + (uint64_t)1024*1024*8*128) > heap_start_) {
            printf("metadata out of bound\n");
        }

        // [TODO] Why there is a 1?
        section_e init_section_header = {0,0, 0, 0, 0, 1, 0, 0};
        region_e init_region_header = {0, 0, 0, 0, 0, 1, 0, 0};

        for(int i = 0; i < section_num_; i++) {
            section_header_[i].store(init_section_header);
        }
        
        for(int i = 0; i < region_num_; i++) {
            region_header_[i].store(init_region_header);
        }
        block_e blank = {0,0};
        for(int i = 0; i < block_num_; i++) {
            block_rkey_[i].store({0,0});
        }
        std::random_device e;
        mt.seed(e());

        return true;
    }

    inline bool ServerBlockManager::check_section(section_e alloc_section, alloc_advise advise, uint32_t offset) {
        switch (advise) {
        case alloc_empty:
            return ((~alloc_section.alloc_map_ & ~alloc_section.frag_map_) & (bitmap16)1<< offset) != 0;
        case alloc_light:
            return ((alloc_section.alloc_map_ & ~alloc_section.frag_map_) & (bitmap16)1<< offset) != 0;
        case alloc_heavy:
            return ((~alloc_section.alloc_map_ & alloc_section.frag_map_) & (bitmap16)1<< offset) != 0;
        case alloc_full:
            return ((alloc_section.alloc_map_ & alloc_section.frag_map_) & (bitmap16)1<< offset) != 0;
        }   
        return false;
    }

    bool ServerBlockManager::force_update_section_state(section_e &section, uint32_t region_index, alloc_advise advise) {
        uint64_t section_offset = region_index/region_per_section;
        uint64_t region_offset = region_index%region_per_section;
        section_e section_new = section;
        section = section_header_[section_offset].load();
        if(advise == alloc_full) {
            do{
                if(check_section(section, advise, region_offset)){
                    return false;
                } else if (check_section(section, alloc_empty, region_offset)){
                    return false;
                }
                section_new = section;
                section_new.alloc_map_ |= (uint16_t)1 << region_offset;
                section_new.frag_map_ |= (uint16_t)1 << region_offset;
            }while(!section_header_[section_offset].compare_exchange_strong(section, section_new));
            return true;
        } else if(advise == alloc_empty) {
            do{
                if(check_section(section, advise, region_offset)){
                    return true;
                } else if (check_section(section, alloc_empty, region_offset)){
                    return false;
                }
                section_new = section;
                section_new.alloc_map_ &= ~((bitmap16)1 << region_offset);
                section_new.frag_map_ &= ~((bitmap16)1 << region_offset);
            }while(!section_header_[section_offset].compare_exchange_strong(section, section_new));
            return true;
        } else if(advise == alloc_light) {
            do{
                if(check_section(section, advise, region_offset)){
                    return true;
                } else if (check_section(section, alloc_empty, region_offset)){
                    return false;
                }
                section_new = section;
                section_new.frag_map_ &= ~((bitmap16)1 << region_offset);
                section_new.alloc_map_ |= (uint16_t)1 << region_offset;
            }while(!section_header_[section_offset].compare_exchange_strong(section, section_new));
            return true;
        } else if(advise == alloc_heavy) {
            do{
                if(check_section(section, advise, region_offset)){
                    return true;
                } else if (check_section(section, alloc_empty, region_offset)){
                    return false;
                }
                section_new = section;
                section_new.frag_map_ |= (uint16_t)1 << region_offset;
                section_new.alloc_map_ &= ~((bitmap16)1 << region_offset);
            }while(!section_header_[section_offset].compare_exchange_strong(section, section_new));
            return true;
        }
        return false;
    }

    bool ServerBlockManager::force_update_section_state(section_e &section, uint32_t region_index, alloc_advise advise, alloc_advise compare) {
        uint64_t section_offset = region_index/region_per_section;
        uint64_t region_offset = region_index%region_per_section;
        section_e section_new = section;
        section = section_header_[section_offset].load();
        if(advise == alloc_full) {
            do{
                if(check_section(section, compare, region_offset)){
                    return false;
                } else if (check_section(section, alloc_empty, region_offset)){
                    return false;
                }
                section_new = section;
                section_new.alloc_map_ |= (uint16_t)1 << region_offset;
                section_new.frag_map_ |= (uint16_t)1 << region_offset;
            }while(!section_header_[section_offset].compare_exchange_strong(section, section_new));
            return true;
        } else if(advise == alloc_empty) {
            do{
                if(check_section(section, compare, region_offset)){
                    return false;
                } else if (check_section(section, alloc_empty, region_offset)){
                    return false;
                }
                section_new = section;
                section_new.alloc_map_ &= ~((bitmap16)1 << region_offset);
                section_new.frag_map_ &= ~((bitmap16)1 << region_offset);
            }while(!section_header_[section_offset].compare_exchange_strong(section, section_new));
            return true;
        } else if(advise == alloc_light) {
            do{
                if(check_section(section, compare, region_offset)){
                    return false;
                } else if (check_section(section, alloc_empty, region_offset)){
                    return false;
                }
                section_new = section;
                section_new.frag_map_ &= ~((bitmap16)1 << region_offset);
                section_new.alloc_map_ |= (uint16_t)1 << region_offset;
            }while(!section_header_[section_offset].compare_exchange_strong(section, section_new));
            return true;
        } else if(advise == alloc_heavy) {
            do{
                if(check_section(section, compare, region_offset)){
                    return false;
                } else if (check_section(section, alloc_empty, region_offset)){
                    return false;
                }
                section_new = section;
                section_new.frag_map_ |= (uint16_t)1 << region_offset;
                section_new.alloc_map_ &= ~((bitmap16)1 << region_offset);
            }while(!section_header_[section_offset].compare_exchange_strong(section, section_new));
            return true;
        }
        return false;
    }

    int ServerBlockManager::find_section(section_e &alloc_section, uint32_t &section_offset, uint16_t size_class, alloc_advise advise) {
        int retry_time = 0;
        section_e section = {0,0};
        int offset = (section_offset + mt()) % section_num_;

        if(advise == alloc_heavy) {
            int remain = section_num_, fetch = (offset + 8) > section_num_ ? (section_num_ - offset):8, index = offset;
            while(remain > 0) {
                retry_time++;
                for(int j = 0; j < fetch; j++) {
                    section = section_header_[index+j].load();
                    if((section.frag_map_ & section.alloc_map_) != bitmap16_filled){
                        alloc_section = section;
                        section_offset = index + j;
                        return true;
                    }
                    index = (index + fetch)%section_num_; remain -= fetch; fetch = (index + 8) > section_num_ ? (section_num_ - index):8;
                }
            }
        } else if(advise == alloc_light) {
            int remain = section_num_, fetch = (offset + 8) > section_num_ ? (section_num_ - offset):8, index = offset;
            while(remain > 0) {
                retry_time++;
                for(int j = 0; j < fetch; j++) {
                    section = section_header_[index+j].load();
                    if((section.frag_map_) != bitmap16_filled){
                        alloc_section = section;
                        section_offset = index + j;
                        return true;
                    }
                    index = (index + fetch)%section_num_; remain -= fetch; fetch = (index + 8) > section_num_ ? (section_num_ - index):8;
                }
            }
        } else {
            int remain = section_num_, fetch = (offset + 8) > section_num_ ? (section_num_ - offset):8, index = offset;
            while(remain > 0) {
                for(int j = 0; j < fetch; j++) {
                    section = section_header_[index+j].load();
                    if((section.frag_map_ | section.alloc_map_) != bitmap16_filled){
                        alloc_section = section;
                        section_offset = index + j;
                        return true;
                    }
                    index = (index + fetch)%section_num_; remain -= fetch; fetch = (index + 8) > section_num_ ? (section_num_ - index):8;
                }
            }
        }
    }

    /*  giving section header information, and current region search index
        fetch region for different allocation requests:
        size < 5: search inner a region
        size >= 5: find several region

    */

    int ServerBlockManager::fetch_region(section_e &alloc_section, uint32_t section_offset, uint16_t size_class, bool use_chance, region_e &alloc_region, uint32_t &region_index, uint32_t skip_mask) {
        int retry_time = 0;
        // if(shared == false) {
        //     // force use unclassed one to alloc single block
        //     section_e new_section;
        //     uint16_t free_map;
        //     int index;
        //     do {
        //         retry_time++;
        //         free_map = alloc_section.frag_map_ | alloc_section.alloc_map_;
        //         if( (index = find_free_index_from_bitmap32_tail(free_map)) == -1 ){
        //             return retry_time*(-1);
        //         }
        //         new_section = alloc_section;
        //         new_section.alloc_map_ |= ((uint16_t)1<<index);
        //         new_section.frag_map_ |= ((uint16_t)1<<index);
        //     }while (!section_header_[section_offset].compare_exchange_strong(alloc_section, new_section));
        //     alloc_section = new_section;
        //     region_e region_new, region_old;
        //     region_index = section_offset*region_per_section+index;
        //     alloc_region = region_header_[region_index].load();
        //     do {
        //         retry_time++;
        //         region_new = region_old;
        //         if(region_new.exclusive_ == 1) {
        //             printf("impossible problem: exclusive is already set\n");
        //             return retry_time*(-1);
        //         }
        //         region_new.exclusive_ = 1;
        //         region_new.on_use_ = 1;
        //     }while(!region_header_[region_index].compare_exchange_strong(region_old, region_new));
        //     region_old = region_new;
        //     alloc_region = region_old;
        //     return retry_time;
        // } else {
        bool on_empty = false;
        section_e new_section;
        uint16_t empty_map, chance_map, normal_map;
        int index;
        do {
            retry_time++;
            int rand_val = mt()%16;
            uint16_t random_frag = (alloc_section.frag_map_ >> (16 - rand_val) | (alloc_section.frag_map_ << rand_val));
            uint16_t random_alloc = (alloc_section.alloc_map_ >> (16 - rand_val) | (alloc_section.alloc_map_ << rand_val));
            empty_map = random_frag | random_alloc;
            chance_map = ~random_frag | random_alloc;
            normal_map = random_frag | ~random_alloc;
            if( (index = find_free_index_from_bitmap16_tail(normal_map)) != -1 ){
                // no modify on map status
                index = (index - rand_val + 16) % 16;
                new_section = alloc_section;
                on_empty = false;
            } else if( (index = find_free_index_from_bitmap16_tail(empty_map)) != -1 ){
                // mark the chance map to full
                index = (index - rand_val + 16) % 16;
                new_section = alloc_section;
                raise_bit(new_section.alloc_map_, new_section.frag_map_, index);
                on_empty = true;
            } else if( (index = find_free_index_from_bitmap16_tail(chance_map)) != -1 ){
                // mark the empty map to allocated
                index = (index - rand_val + 16) % 16;
                new_section = alloc_section;
                on_empty = false;
            } else {
                return retry_time*(-1);
            }
        }while (!section_header_[section_offset].compare_exchange_strong(alloc_section, new_section));
        region_e region_new;
        alloc_section = new_section;
        region_index = section_offset*region_per_section+index;
        alloc_region = region_header_[region_index].load();
        if(on_empty){
            do {
                retry_time++;
                region_new = alloc_region;
                if(region_new.on_use_ == 1) {
                    printf("impossible problem: exclusive is already set\n");
                    return retry_time*(-1);
                }
                region_new.on_use_ = 1;
            } while (!region_header_[region_index].compare_exchange_strong(alloc_region, region_new));
        }
        return retry_time;
        // }
        return 0;
    }

    int ServerBlockManager::fetch_region_block(section_e &alloc_section, region_e &alloc_region, uint64_t &addr, uint32_t &rkey, bool is_exclusive, uint32_t region_index, uint16_t block_class) {
        int index, retry_time =0; region_e new_region;
        // uint8_t old_length, new_length;
        do{
            retry_time++;
            if(alloc_region.exclusive_ != is_exclusive || alloc_region.on_use_ != 1) {
                // printf("Region not avaliable, addr = %lx, exclusive = %d\n", get_region_addr(region_index), alloc_region.exclusive_);
                return retry_time*(-1);
            } 
            new_region = alloc_region;
            if((index = find_free_index_from_bitmap32_tail(alloc_region.base_map_)) == -1) {
                return retry_time*(-1);
            }
            new_region.base_map_ |= (uint32_t)1<<index;
            retry_counter_ = new_region.retry_+1;
            new_region.retry_ = retry_time-1;
            // update the max length info
            // old_length = new_region.max_length_;
            // new_length = max_longbit(new_region.base_map_);
            // new_region.max_length_ = new_length;

        } while (!region_header_[region_index].compare_exchange_strong(alloc_region, new_region));
        
        alloc_region = new_region;
        addr = get_region_block_addr(region_index, index);
        rkey = get_region_block_rkey(region_index, index);
        
        // retry counter for least 3 time allocation
        uint64_t old_retry = retry_counter_;
        retry_counter_ = retry_time;

        // concurrency state update, will async it in the future
        if(alloc_region.base_map_ == bitmap32_filled) {
            while(!force_update_section_state(alloc_section, region_index, alloc_full));
        } 
        else if(old_retry >= low_threshold && retry_time < low_threshold) {
            force_update_section_state(alloc_section, region_index, alloc_heavy, alloc_light);
            // printf("make region %d heavy\n", region_index);
        } 
        else if(old_retry < retry_threshold && retry_time >= retry_threshold) {
            force_update_section_state(alloc_section, region_index, alloc_light, alloc_heavy);
            // printf("make region %d light\n", region_index);
        } 
        return retry_time;
    }

    int ServerBlockManager::free_region_block(uint64_t addr, bool is_exclusive, uint16_t block_class) {
        uint32_t region_offset = (addr - heap_start_) / region_size_;
        uint32_t region_block_offset = (addr - heap_start_) % region_size_ / block_size_;
        region_e new_region, region = region_header_[region_offset].load();
        int retry_time = 0;

        if(!region.exclusive_ && is_exclusive) {
            printf("exclusive error, the actual block is shared\n");
            return -1;
        }
        bool full = (region.base_map_ == bitmap32_filled);
        uint32_t new_rkey;
        if((region.base_map_ & ((uint32_t)1<<region_block_offset)) == 0) {
            printf("already freed\n");
            return -1;
        }
        do{
            full = (region.base_map_ == bitmap32_filled);
            retry_time++;
            new_region = region;
            new_region.base_map_ &= ~(uint32_t)(1<<region_block_offset);
            if(new_region.base_map_ == (bitmap32)0) {
                new_region.on_use_ = 0;
                new_region.exclusive_ = 0;
            }
        } while(!region_header_[region_offset].compare_exchange_strong(region, new_region));

        // retry counter for least 3 time allocation
        uint16_t old_retry = retry_counter_+1;
        retry_counter_ = retry_time-1;
        
        // concurrency state update, will async it in the future
        section_e alloc_section;
        if(!is_exclusive && new_region.base_map_ == (bitmap32)0 ){
            force_update_section_state(alloc_section, region_offset, alloc_empty); 
            return -2;
        } else if(full) {
            while(!force_update_section_state(alloc_section, region_offset, alloc_light, alloc_full)){
                printf("make region %d light failed\n", region_offset);
            }
        } 
        else if(old_retry < low_threshold && retry_time >= low_threshold) {
            force_update_section_state(alloc_section, region_offset, alloc_heavy, alloc_light);
            // printf("make region %d heavy\n", region_offset);
        } 
        else if(old_retry >= retry_threshold && retry_time < retry_threshold) {
            force_update_section_state(alloc_section, region_offset, alloc_light, alloc_heavy);
            // printf("make region %d light\n", region_offset);
        } 
        region = new_region;
        return 0;
    }

    bool ClientBlockManager::init(uint64_t addr, uint64_t size, uint32_t rkey){
        if (size % block_size_ != 0) {
            printf("Error: FreeQueueManager only support size that is multiple of %ld \n", block_size_);
            return false;
        }
        size_ = size;
        if(size > 0) {
            for(uint64_t i = 0; i < size / block_size_; i++) {
                free_block_queue_.push({addr + i * block_size_, rkey});
                size_ += block_size_;
            }
        }
        total_used_ = 0;
        return true;
    }

    bool ClientBlockManager::fetch(uint64_t size, uint64_t &addr, uint32_t &rkey) {
        std::unique_lock<std::mutex> lock(m_mutex_);
        if(size == block_size_){
            return fetch_block(addr, rkey);
        } else {
            perror("alloc failed, no free space\n");
            return false;
        }
    }

    bool ClientBlockManager::fill_block(uint64_t addr, uint64_t size, uint32_t rkey) {
        std::unique_lock<std::mutex> lock(m_mutex_);
        for(uint64_t i = 0; i < size / block_size_; i++){
            free_block_queue_.push({addr + i * block_size_, rkey});
        }
        return true;    
    }

    bool ClientBlockManager::fetch_block(uint64_t &addr, uint32_t &rkey){
        std::unique_lock<std::mutex> lock(m_mutex_);
        if(free_block_queue_.empty()){
            perror("no enough cache!\n");
            return false;
        }
        remote_addr rem_addr = free_block_queue_.front();
        free_block_queue_.pop();
        total_used_ += block_size_;
        addr = rem_addr.addr; rkey = rem_addr.rkey;
        return true;
    }

    void ClientBlockManager::print_state() {
        printf("mem used: %lu MiB\n", total_used_/1024/1024);
    }

    bool ServerBlockManagerv2::init(uint64_t meta_addr, uint64_t addr, uint64_t size, uint32_t rkey) {
        // uint64_t align = block_size_*large_block_items < 1024*1024*2 ? 1024*1024*2 : block_size_*large_block_items;
        // uint64_t base_align = block_size_ < 1024*1024*2 ? 1024*1024*2 : block_size_;
        // assert(meta_addr % base_align == 0 && addr % base_align == 0 && size % align == 0);
        // large_block_num = size/align;
        // block_info = (large_block*)meta_addr;
        // heap_start = addr;
        // heap_size = size;
        // global_rkey = rkey;
        // block_header_e header_; 
        // header_.alloc_history = 0;
        // header_.max_length = block_size_/base_size;
        // header_.flag &= (uint32_t)0;
        // header_.bitmap &= (uint32_t)0;
        // free_list.store(nullptr);
        // for(int i = large_block_num-1; i >= 0; i--){
        //     uint64_t bitmap_init = (uint64_t)0;
        //     block_info[i].bitmap.store(bitmap_init);
        //     for(int j = 0; j < large_block_items; j++){
        //         block_info[i].header[j].store(header_);
        //         block_info[i].rkey[j] = 0;
        //     }
        //     block_info[i].next = free_list.load();
        //     while(!free_list.compare_exchange_strong(block_info[i].next, &block_info[i]));
        //     block_info[i].offset = i;
        // }
        // last_alloc = 0;
        // return true;
    }

    uint64_t find_free_index_from_bitmap(uint64_t bitmap) {
        return __builtin_ctzll(~bitmap);
    }

    bool ServerBlockManagerv2::fetch_block(uint64_t &addr, uint32_t &rkey) {
        // uint64_t block_index, free_index;
        // block_header_e header_old, header_new;
        // large_block* free = free_list.load();
        // while(free != nullptr) {
        //     uint64_t bitmap_, bitmap_new_;
        //     if(free->offset >= user_start && (bitmap_ = free->bitmap.load()) !=  ~(uint64_t)0) {
        //         do{
        //             bitmap_new_ = bitmap_;
        //             free_index = find_free_index_from_bitmap(bitmap_);
        //             bitmap_new_ |= (uint64_t)1<<free_index;
        //         } while (!free->bitmap.compare_exchange_strong(bitmap_, bitmap_new_) && (bitmap_) != ~(uint64_t)0);
        //         if(bitmap_ != ~(uint64_t)0) {
        //             addr = heap_start + (free->offset*large_block_items + free_index)*block_size_;
        //             rkey = free->rkey[free_index];
        //             return true;
        //         }
        //     }
        //     free_list.compare_exchange_strong(free, free->next);
        // }
        // printf("freelist failed!\n");
        // for(int i = 0;i < large_block_num; i++){
        //     block_index = (i + last_alloc ) % large_block_num;  
        //     // load bitmap, and check if there are valid bit
        //     while((free_index = find_free_index_from_bitmap(block_info[block_index].bitmap.load())) != 64) {
        //         // find valid bit, try to allocate
        //         header_old = block_info[block_index].header[free_index].load();
        //         if(header_old.flag % 2 == 1)
        //             continue;
        //         header_new = header_old; header_new.flag |= 1;
        //         if(!block_info[block_index].header[free_index].compare_exchange_strong(header_old, header_new)){
        //             continue;
        //         }
        //         last_alloc = block_index;
        //         block_info[block_index].bitmap.fetch_or((uint64_t)1<<free_index);
        //         addr = heap_start + (block_index*large_block_items + free_index)*block_size_;
        //         rkey = block_info[block_index].rkey[free_index];
        //         return true;
        //     }
        // }
        // return false;
    }

    bool ServerBlockManagerv2::fetch(uint64_t start_addr, uint64_t size, uint64_t &addr, uint32_t &rkey) {
        // uint64_t start_index = get_block_index(start_addr);
        // uint64_t end_index = get_block_index(start_addr + size - 1);
        // for (int i = start_index; i <= end_index; i++) {
        //     block_header_e header_old = block_info[i/64].header[i%64].load();
        //     if (header_old.flag % 2 == 1 || (header_old.bitmap | (uint32_t)0) != 0){
        //         printf("Fixed start addr align malloc failed!\n");
        //         return false;
        //     }
        // }
        // std::unique_lock<std::mutex> lock(m_mutex_);
        // for (int i = start_index; i <= end_index; i++) {
        //     block_header_e header_old = block_info[i/64].header[i%64].load();
        //     block_header_e header_new = header_old;
        //     assert(header_old.flag % 2 == 0);
        //     header_new.flag |= (uint64_t)1;
        //     if (!block_info[i/64].header[i%64].compare_exchange_strong(header_old, header_new)){
        //         printf("malloc failed!\n");
        //         return false;
        //     }
        //     block_info[i/64].bitmap.fetch_or((uint64_t)1<<(i%64));
        // }
        // rkey = global_rkey;
        // addr = get_block_addr(start_index);
        // user_start = end_index/64 + 1;
        // printf("last alloc:%lu\n", last_alloc);
        // return true;
        
    }

    bool ServerBlockManagerv1::init(uint64_t addr, uint64_t size, uint32_t rkey) {
        large_block_num = size/block_size_;
        uint64_t metadata_size = large_block_num*sizeof(header_list);
        uint64_t rkey_size = large_block_num*sizeof(uint32_t);
        uint64_t block_align_offset = (metadata_size + rkey_size - 1)/block_size_*block_size_ + block_size_;
        large_block_num -= block_align_offset/block_size_;
        header_list = (block_header*)addr;
        block_header_e header_, old_header; 
        header_.alloc_history = 0;
        header_.max_length = block_size_/base_size;
        header_.flag |= (uint32_t)1;
        header_.bitmap &= (uint32_t)0;
        for(int i=0; i<large_block_num; i++){
            do{
                old_header = header_list[i].load();
            } while(!header_list[i].compare_exchange_weak(old_header, header_));
        }
        block_rkey_list = (uint32_t*)(addr + metadata_size);
        heap_start = addr + block_align_offset;
        heap_size = size - block_align_offset;
        global_rkey = rkey;
        last_alloc = 0;
        return true;
    }
    
    bool ServerBlockManagerv1::fetch_block(uint64_t &addr, uint32_t &rkey) {
        block_header* header = get_metadata();
        uint64_t last_alloc_ = last_alloc.load();
        for(int i = 0; i< large_block_num-user_start; i++){
            uint64_t index = (i+last_alloc_)%(large_block_num - user_start) + user_start;
            block_header_e header_old = header[index].load();
            if(header_old.max_length == block_size_/base_size && (header_old.flag & (uint16_t)1) == 1){
                block_header_e update_header = header_old;
                update_header.flag &= ~((uint16_t)1);
                if(header[index].compare_exchange_strong(header_old, update_header)){
                    addr = get_block_addr(index);
                    rkey = global_rkey;
                    while(!last_alloc.compare_exchange_strong(last_alloc_, index + 1));
                    return true;
                }
            }
        }
        return false;
    }

    bool ServerBlockManagerv1::fetch(uint64_t start_addr, uint64_t size, uint64_t &addr, uint32_t &rkey) {
        uint64_t start_index = get_block_index(start_addr);
        uint64_t end_index = get_block_index(start_addr + size - 1);
        block_header* header = get_metadata();
        for (int i = start_index; i <= end_index; i++) {
            block_header_e header_old = header[i].load();
            if ((header_old.flag & (uint64_t)1) != 1 || (header_old.bitmap | (uint32_t)0) != 0){
                printf("Fixed start addr align malloc failed!\n");
                return false;
            }
        }
        std::unique_lock<std::mutex> lock(m_mutex_);
        for (int i = start_index; i <= end_index; i++) {
            block_header_e header_old = header[i].load();
            block_header_e header_new = header_old;
            assert((header_old.flag & (uint16_t)1) == (uint16_t)1);
            header_new.flag &= ~((uint64_t)1);
            if (!header[i].compare_exchange_strong(header_old, header_new)){
                printf("malloc failed!\n");
                return false;
            }
        }
        rkey = global_rkey;
        addr = get_block_addr(start_index);
        user_start = end_index + 1;
        printf("last alloc:%lu\n", last_alloc.load());
        return true;
    }

    bool FreeQueueManager::init(mr_rdma_addr addr, uint64_t size){
        std::unique_lock<std::mutex> lock(m_mutex_);
        if (size != pool_size_){
            printf("Error: FreeQueueManager only support size that is multiple of %ld \n", block_size_);
            return false;
        }
        // uint64_t cache_size = std::min(queue_watermark, size);
        // raw_heap = addr.addr;
        // raw_size = size;
        // raw_rkey = addr.rkey;
        // raw_node = addr.node;
        // uint64_t start_addr = addr.addr + raw_size - cache_size;
        free_bitmap_[{addr.addr-addr.addr%((uint64_t)pool_size_), 0, addr.node}]= (uint64_t*)calloc(1024, sizeof(uint64_t));
        for(uint64_t i = 0; i < size / block_size_; i++){
            free_block_queue.push({addr.addr + i * block_size_, addr.rkey, addr.node});
        }
        total_used = 0;
        return true;
    }

    bool FreeQueueManager::fetch(uint64_t size,  mr_rdma_addr &addr) {
        std::unique_lock<std::mutex> lock(m_mutex_);
        if(size == block_size_){
            return fetch_block(addr);
        // }
        // else if (size <= raw_size) {
        //     uint64_t raw_alloc = raw_heap;
        //     raw_heap += size;
        //     raw_size -= size;
        //     total_used += size;
        //     addr.addr = raw_alloc; addr.rkey = raw_rkey; addr.node = raw_node;
        //     return true;
        } else {
            perror("alloc failed, no free space\n");
            return false;
        }
    }

    bool FreeQueueManager::fill_block(mr_rdma_addr addr, uint64_t size) {
        std::unique_lock<std::mutex> lock(m_mutex_);
        if (size != pool_size_){
            printf("Error: FreeQueueManager only support size that is multiple of %ld \n", block_size_);
            return false;
        }
        // if (0) {
        //     raw_heap -= size;
        //     raw_size += size;
        //     return true;
        // } else 
        free_bitmap_[{addr.addr-addr.addr%((uint64_t)pool_size_), 0, addr.node}] = (uint64_t*)calloc(2048, sizeof(uint64_t));
        for(uint64_t i = 0; i < size / block_size_; i++){
            free_block_queue.push({addr.addr + i * block_size_, addr.rkey, addr.node});
        }
        return true;    
    }

    bool FreeQueueManager::fetch_block(mr_rdma_addr &addr){
        std::unique_lock<std::mutex> lock(m_mutex_);
        if(free_block_queue.empty()){
            // if(raw_size >= block_size_){
            //     free_block_queue.push({raw_heap + raw_size - block_size_, raw_rkey, raw_node});
            //     raw_size -= block_size_;
            // } else {
            return false;
            // }
        }
        mr_rdma_addr index = {0,0,0};
        uint64_t offset;
        do{
            if(free_block_queue.empty()){
                return false;
            }
            addr = free_block_queue.front();
            free_block_queue.pop();
            index.addr = addr.addr; index.node = addr.node;
            offset = index.addr % pool_size_ / block_size_;
            index.addr -= index.addr % pool_size_;
        }while(free_bitmap_.find(index) == free_bitmap_.end());
        free_bitmap_[index][offset/64] |= (uint64_t)1<<(offset%64);
        total_used += block_size_;
        return true;
    }

    bool FreeQueueManager::return_block(mr_rdma_addr addr, bool &all_free){
        std::unique_lock<std::mutex> lock(m_mutex_);
        free_block_queue.push(addr);
        mr_rdma_addr index = {addr.addr, 0, addr.node};
        uint64_t offset = index.addr % pool_size_ / block_size_;
        index.addr -= index.addr % pool_size_;
        free_bitmap_[index][offset/64] &= ~((uint64_t)1<<(offset%64));
        all_free = true;
        for(int i = 0; i < pool_size_/block_size_/64 +1 ; i++) {
            if(free_bitmap_[index][i] != (uint64_t)0){
                all_free=false;
                break;
            }
        }
        if(all_free){
            printf("%lu\n", free_bitmap_[index][0]);
            free(free_bitmap_[index]);
            free_bitmap_.erase(index);

            // for(int i = 0; i < pool_size_/block_size_/64 ; i++) {
            //     free_bitmap_[index][i] = ~((uint64_t)0);
            // }   
        }
        total_used -= block_size_;
        return true;
    }

    bool FreeQueueManager::return_block_no_free(mr_rdma_addr addr, bool &all_free){
        std::unique_lock<std::mutex> lock(m_mutex_);
        free_block_queue.push(addr);
        mr_rdma_addr index = {addr.addr, 0, addr.node};
        uint64_t offset = index.addr % pool_size_ / block_size_;
        index.addr -= index.addr % pool_size_;
        free_bitmap_[index][offset/64] &= ~((uint64_t)1<<(offset%64));
        all_free = false;
        total_used -= block_size_;
        return true;
    }

    void FreeQueueManager::print_state() {
        printf("mem used: %lu MiB\n", total_used/1024/1024);
    }

}
