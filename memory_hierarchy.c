/*************************************************************************************|
|   1. YOU ARE NOT ALLOWED TO SHARE/PUBLISH YOUR CODE (e.g., post on piazza or online)|
|   2. Fill main.c and memory_hierarchy.c files                                       |
|   3. Do not use any other .c files neither alter main.h or parser.h                 |
|   4. Do not include any other library files                                         |
|*************************************************************************************/
#include "mipssim.h"

/// @students: declare cache-related structures and variables here
uint8_t *cache;
uint32_t number_rows;
uint32_t index_length;


void memory_state_init(struct architectural_state* arch_state_ptr) {
    arch_state_ptr->memory = (uint32_t *) malloc(sizeof(uint32_t) * MEMORY_WORD_NUM);
    memset(arch_state_ptr->memory, 0, sizeof(uint32_t) * MEMORY_WORD_NUM);
    if(cache_size == 0){
        // CACHE DISABLED
        memory_stats_init(arch_state_ptr, 0); // WARNING: we initialize for no cache 0
    }else {
        // CACHE ENABLED
        /// @students: memory_stats_init(arch_state_ptr, X); <-- fill # of tag bits for cache 'X' correctly
        // 32 bit address
        // 16 byte blocks -> 4 offset bits
        // number rows = cache_size / 16
        // -> (log2(number_rows)) index bits
        number_rows = cache_size / 16;
        upper_ceiling_rows = 2*number_rows - 1;
        index_length = log10(upper_ceiling_rows) / log10(2);
        int tag_length = 32 - 4 - index_length;
        printf("The tag has length %d\n", tag_length);
        memory_stats_init(arch_state_ptr, tag_length);
          printf("I repeat: The tag has length %d\n", (*arch_state_ptr).bits_for_cache_tag);


        // one row in the cache has the bit-size of (upper bound of tag_length / 8)*8 + 16*8 + 1*8 ............. for the tag + for the data and for the valid bit, respectively
        cache =  malloc(sizeof(uint8_t) * number_rows * (((*arch_state_ptr).bits_for_cache_tag + 7) / 8 + 16 + 1) );
        memset(cache, 0, sizeof cache);
    }
}


// returns data on memory[address / 4]
int memory_read(int address){
    arch_state.mem_stats.lw_total++;
    check_address_is_word_aligned(address);

    if(cache_size == 0){
        // CACHE DISABLED
        return (int) arch_state.memory[address / 4];
    }else{
        // CACHE ENABLED
        uint8_t byte_offset = get_piece_of_a_word(address, 0 , 4);
        // Neither index nor tag can ever exceed 32 bits, since we have 32 bit addresses
        uint32_t index = get_piece_of_a_word(address, 4 , index_length);
        uint32_t tag = get_piece_of_a_word(address, 4+index_length , 32 - 4 -index_length);
        printf("byte offset of what we are looking for is %d\n", byte_offset);
        printf("index of what we are looking for is %d\n", index);
        printf("tag of what we are looking for is %d\n", tag);

        uint8_t num_bytes_for_tag = (arch_state.bits_for_cache_tag + 7) / 8;
        uint8_t *valid_byte_pnt = cache + index * ( 17 + num_bytes_for_tag );

        uint8_t valid = *(valid_byte_pnt);
        printf("validity of the cache entry is %d\n", valid);
        printf("We use %d (%d) bits for the tag \n", num_bytes_for_tag*8 , arch_state.bits_for_cache_tag);

        if (valid == 0) {
          printf("We have %d load hits out of %d attempts\n", arch_state.mem_stats.lw_cache_hits , arch_state.mem_stats.lw_total);
            // We set the valid bit to 1.
            uint8_t fresh_valid = 1;
            *(valid_byte_pnt) = fresh_valid;

            // We store the tag. We store each byte we require individually. So far, we can only reserve multiples of 8 bits for the tag,
            // which means we might be wasting up to 7 bits of cache space for the tag. I shall see if this can be improved in an elegant way.
            for (int i = 0; i < num_bytes_for_tag; i++) {
                *(valid_byte_pnt + 1 + i) = get_piece_of_a_word(tag, 8*i, 8);
            }

            // We store each of the 16 bytes individually, and then return the word at the appropriate byte offset, which is a 4 byte block
            // address / 4 - ((address / 4) % 4) computes the start of the block of data we wish to store. address/4 lies somewhere in the 16 byte block
            for (int i = 0; i < 16; i++) {
                *(valid_byte_pnt + 1 + num_bytes_for_tag + i ) = get_piece_of_a_word(arch_state.memory[address / 4 - ((address / 4) % 4) + i / 4], 8*((3-i) % 4), 8);
            }

        } else {
            if (valid == 1) {

                // We get the tag of the block of data currently in the cache at the releant index
                uint32_t current_tag = 0;
                for (int i = 0; i < num_bytes_for_tag; i++) {
                    current_tag += *(valid_byte_pnt + 1 + i) << (8*(num_bytes_for_tag - i));
                }
                printf("The current tag is %d\n", current_tag);

                // If the tags match, we have a hit and return what is already in the cache
                if (tag != current_tag){
                  printf("We have %d load hits out of %d attempts\n", arch_state.mem_stats.lw_cache_hits , arch_state.mem_stats.lw_total);
                  // If we have a miss, we load the needed block of data into the cache, replacing the old one

                  for (int i = 0; i < 16; i++) {
                      *(valid_byte_pnt + 1 + num_bytes_for_tag + i ) = get_piece_of_a_word(arch_state.memory[address / 4 - ((address / 4) % 4) + i / 4], 8*((3-i) % 4), 8);
                  }

                  // Of course, we also store the new tag
                  for (int i = 0; i < num_bytes_for_tag; i++) {
                      *(valid_byte_pnt + 1 + i) = get_piece_of_a_word(tag, 8*i, 8);
                  }

                } else{
                    arch_state.mem_stats.lw_cache_hits++;
                    printf("We have %d load hits out of %d attempts\n", arch_state.mem_stats.lw_cache_hits , arch_state.mem_stats.lw_total);
                }

            } else {
                assert(0);
            }
        }

        /// @students: your implementation must properly increment: arch_state_ptr->mem_stats.lw_cache_hits
        return (int) (*(valid_byte_pnt + 1 + num_bytes_for_tag + byte_offset) << 24) + (*(valid_byte_pnt + 1 + num_bytes_for_tag + byte_offset + 1) << 16)
              + (*(valid_byte_pnt + 1 + num_bytes_for_tag + byte_offset + 2) << 8) + (*(valid_byte_pnt + 1 + num_bytes_for_tag + byte_offset + 3));
    }
    return 0;
}

// writes data on memory[address / 4]
void memory_write(int address, int write_data){
    arch_state.mem_stats.sw_total++;
    check_address_is_word_aligned(address);

    if(cache_size == 0){
        // CACHE DISABLED
        arch_state.memory[address / 4] = (uint32_t) write_data;
        return;
    }else{
      //CACHE ENABLED
      //assert(0); /// @students: Remove assert(0); and implement Memory hierarchy w/ cache
      uint8_t byte_offset = get_piece_of_a_word(address, 0 , 4);
      // Neither index nor tag can ever exceed 32 bits, since we have 32 bit addresses
      uint32_t index = get_piece_of_a_word(address, 4 , index_length);
      uint32_t tag = get_piece_of_a_word(address, 4+index_length , 32 - 4 -index_length);
      printf("byte offset of what we are looking for is %d\n", byte_offset);
      printf("index of what we are looking for is %d\n", index);
      printf("tag of what we are looking for is %d\n", tag);

      uint8_t num_bytes_for_tag = (arch_state.bits_for_cache_tag + 7) / 8;
      uint8_t *valid_byte_pnt = cache + index * ( 17 + num_bytes_for_tag );

      uint8_t valid = *(valid_byte_pnt);
      printf("validity of the cache entry is %d\n", valid);
      printf("We use %d (%d) bits for the tag \n", num_bytes_for_tag*8 , arch_state.bits_for_cache_tag);

      if (valid == 0) {
          printf("We have %d store hits out of %d attempts\n", arch_state.mem_stats.sw_cache_hits , arch_state.mem_stats.sw_total);
          arch_state.memory[address / 4] = (uint32_t) write_data;
          return;
      } else {
        // We get the tag of the block of data currently in the cache at the relevant index
        uint32_t current_tag = 0;
        for (int i = 0; i < num_bytes_for_tag; i++) {
            current_tag += *(valid_byte_pnt + 1 + i) << (8*(num_bytes_for_tag - i));
        }
        printf("The current tag is %d\n", current_tag);

        if (current_tag != tag) {
          printf("We have %d store hits out of %d attempts\n", arch_state.mem_stats.sw_cache_hits , arch_state.mem_stats.sw_total);
          arch_state.memory[address / 4] = (uint32_t) write_data;
          return;
        } else {
            arch_state.mem_stats.sw_cache_hits++;
            printf("We have %d store hits out of %d attempts\n", arch_state.mem_stats.sw_cache_hits , arch_state.mem_stats.sw_total);
            // Store the new word in the cache
            *(valid_byte_pnt + 1 + num_bytes_for_tag + byte_offset + 0) = get_piece_of_a_word((uint32_t) write_data , 24 , 8);
            *(valid_byte_pnt + 1 + num_bytes_for_tag + byte_offset + 1) = get_piece_of_a_word((uint32_t) write_data , 16 , 8);
            *(valid_byte_pnt + 1 + num_bytes_for_tag + byte_offset + 2) = get_piece_of_a_word((uint32_t) write_data ,  8 , 8);
            *(valid_byte_pnt + 1 + num_bytes_for_tag + byte_offset + 3) = get_piece_of_a_word((uint32_t) write_data ,  0 , 8);
            // Store the new word in memory
            arch_state.memory[address / 4] = (uint32_t) write_data;
            return;
        }
      }
    }
}
