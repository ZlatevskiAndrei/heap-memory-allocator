#include "mem_alloc.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


#include "mem_alloc_types.h"
#include "my_mmap.h"


void *heap_begin;

memblk_free_t *first_free; 

memblk_free_t *next_fit_current_block; 

uint8_t final_padding_header;
uint8_t final_padding_payload;
uint8_t final_end_payload_padding;

#define ULONG(x)((long unsigned int)(x))


void run_at_exit(void)
{
    debug_printf("Starting munmap procedure at %s\n", __FUNCTION__);
    int res = my_munmap(heap_begin, MEM_POOL_SIZE);
    first_free = NULL;
    if(res != 0){
        debug_printf("munmap failed, res is %d\n", res);
        exit(0);
    }    
    debug_printf("Exiting %s ...\n", __FUNCTION__);
}


void memory_init(void)
{
    atexit(run_at_exit);
    heap_begin = (uint64_t*) my_mmap(MEM_POOL_SIZE);
    assert(heap_begin != NULL);
    debug_printf("Started memory init at %p\n", heap_begin);
    first_free = (memblk_free_t*)heap_begin;
    first_free->size = (uint32_t) MEM_POOL_SIZE - sizeof(memblk_free_t);
    first_free->next = NULL;
    next_fit_current_block = first_free;
}

uint8_t calculate_header_padding_size(memblk_free_t* free_block){
    uint8_t mod_result = (uint8_t) ((uintptr_t)free_block % MEM_ALIGNMENT);
        
    return mod_result == 0 ? 0 : MEM_ALIGNMENT - mod_result;

}

uint8_t calculate_payload_padding_size(memblk_free_t* free_block) {
    uint8_t* payload_addr = (uint8_t*) free_block + sizeof(memblk_allocated_t);
    uint8_t mod_result = (uint8_t) ((uintptr_t)payload_addr % MEM_ALIGNMENT);
    
    return mod_result == 0 ? 0 : MEM_ALIGNMENT - mod_result; 
}

uint8_t calculate_end_payload_padding_size(uint8_t* payload, size_t size){
    uint8_t* end_payload_addr = payload + size;
    uint8_t mod_result = (uint8_t) ((uintptr_t)end_payload_addr % MEM_ALIGNMENT);

    return mod_result == 0 ? 0 : MEM_ALIGNMENT - mod_result;
}

void *memory_alloc(size_t size){

    if(first_free == NULL) {
        debug_printf("No free space to alloc at %s, size %ld \n", __FUNCTION__, size);
        print_alloc_error(size);
        exit(0);
    }
    
    memblk_free_t *block_fit = NULL;
    memblk_free_t *parent_free_block = NULL;

    #if defined(FIRST_FIT)
    
    memblk_free_t *right = first_free;
    memblk_free_t *left = NULL;
    while (right != NULL) {
        uint32_t full_free = right->size + sizeof(memblk_free_t);
        uint8_t tmp_padding_header = calculate_header_padding_size(right);
        uint8_t tmp_padding_payload = calculate_payload_padding_size((memblk_free_t*)((uint8_t*)right + tmp_padding_header));
        uint8_t tmp_end_padding_payload =  calculate_end_payload_padding_size((uint8_t*) ((uint8_t*) right + tmp_padding_header + sizeof(memblk_allocated_t) + tmp_padding_payload), size);
        
        if (full_free >= size + sizeof(memblk_allocated_t) + tmp_padding_header + tmp_padding_payload + tmp_end_padding_payload) { 
            block_fit = right;
            parent_free_block = left;
            final_padding_header = tmp_padding_header;
            final_padding_payload = tmp_padding_payload;
            final_end_payload_padding = tmp_end_padding_payload;
            break;
        }
        left = right;
        right = right->next;
    }

    #elif defined(BEST_FIT)

    memblk_free_t *right = first_free;
    memblk_free_t *left = NULL;
    memblk_free_t *best_fit_block = NULL;
    memblk_free_t *parent_best_fit_block = NULL;

    uint32_t min_space = UINT32_MAX;

    uint8_t tmp_padding_header;
    uint8_t tmp_padding_payload;
    uint8_t tmp_end_padding_payload;


    while (right!= NULL) {
        uint32_t full_free = right->size + sizeof(memblk_free_t);
        tmp_padding_header = calculate_header_padding_size(right);
        tmp_padding_payload = calculate_payload_padding_size((memblk_free_t*)((uint8_t*)right + tmp_padding_header));
        tmp_end_padding_payload =  calculate_end_payload_padding_size((uint8_t*) ((uint8_t*) right + tmp_padding_header + sizeof(memblk_allocated_t) + tmp_padding_payload), size);
        
        if(full_free >= size + sizeof(memblk_allocated_t) + tmp_padding_header + tmp_padding_payload + tmp_end_padding_payload){
            uint32_t min_space_new = full_free - ((uint32_t)size + sizeof(memblk_allocated_t) + tmp_padding_header + tmp_padding_payload + tmp_end_padding_payload);
            
            if(min_space_new == 0){
                best_fit_block = right;
                parent_best_fit_block = left; 
                break;
            }
                
            else if(min_space_new > 0 && min_space_new < min_space){
                min_space = min_space_new;
                best_fit_block = right;
                parent_best_fit_block = left;     
            }  
        }
        left = right;
        right = right->next;
    }

    final_padding_header = tmp_padding_header;
    final_padding_payload = tmp_padding_payload;
    final_end_payload_padding = tmp_end_padding_payload;
    block_fit = best_fit_block;
    parent_free_block = parent_best_fit_block;


    #elif defined(NEXT_FIT)

    memblk_free_t *left = NULL;
    memblk_free_t *right = first_free;

    if(next_fit_current_block != first_free){ //we find the parent of next_fit_current_block and coalesce if needed
        while (right != NULL && next_fit_current_block != right) {
            left = right;
            right = right->next;
        }
        memblk_free_t *end_left_block=(memblk_free_t*)((uint8_t*)left + sizeof(memblk_free_t) + left->size); 
        if((uint8_t*)end_left_block == (uint8_t*)next_fit_current_block){
            left->size+=next_fit_current_block->size + sizeof(memblk_free_t);
            left->next=next_fit_current_block->next;
            next_fit_current_block = left;
        } 
    }
    
    left = NULL;
    right = next_fit_current_block;
    bool first_loop_immunity = 1; //because we start the loop with right == next_fit_current_block

    while(right != next_fit_current_block || first_loop_immunity){
        first_loop_immunity = 0; 
        if(right == NULL){ //we come back to first_free
            right = first_free;
            left = NULL;
            continue;
        }

        uint32_t full_free = right->size + sizeof(memblk_free_t);
        uint8_t tmp_padding_header = calculate_header_padding_size(right);
        uint8_t tmp_padding_payload = calculate_payload_padding_size((memblk_free_t*)((uint8_t*)right + tmp_padding_header));
        uint8_t tmp_end_padding_payload =  calculate_end_payload_padding_size((uint8_t*) ((uint8_t*) right + tmp_padding_header + sizeof(memblk_allocated_t) + tmp_padding_payload), size);

        if (full_free >= size + sizeof(memblk_allocated_t) + tmp_padding_header + tmp_padding_payload + tmp_end_padding_payload) {
            block_fit = right;
            parent_free_block = left;
            final_padding_header = tmp_padding_header;
            final_padding_payload = tmp_padding_payload;
            final_end_payload_padding = tmp_end_padding_payload;
            next_fit_current_block = block_fit;
            break;
        }
        left = right;
        right = right->next;        
    }

    #endif 

    if (block_fit == NULL) {
        debug_printf("No free block found when calling %s for size = %ld \n", __FUNCTION__, size);
        print_alloc_error(size); 
        exit(0); 
    }
   
    uint32_t free_block_size = block_fit->size;
    uint32_t total_free_block_size = free_block_size + sizeof(memblk_free_t);
    

    uint64_t *result = NULL;
    memblk_free_t *old_next = block_fit->next;

    memblk_allocated_t* new_alloc = (memblk_allocated_t*) block_fit;
    if(free_block_size >= sizeof(memblk_allocated_t) + (uint32_t)size + final_padding_header + final_padding_payload + final_end_payload_padding){ 
        // we split..
        new_alloc = (memblk_allocated_t*) ((uint8_t*)new_alloc + final_padding_header);
        new_alloc->size = size + final_end_payload_padding;
        new_alloc->free = 0;    
        new_alloc->padding_header = final_padding_header;
        new_alloc->payload = (uint64_t*)((uint8_t*)new_alloc + sizeof(memblk_allocated_t) + final_padding_payload); 
        uint8_t* store_padding_payload = (uint8_t*) new_alloc->payload - sizeof(uint8_t);
        *store_padding_payload = final_padding_payload; //2^8 = 256 so 1 mem addr is enough to store this info
        memblk_free_t* new_free = (memblk_free_t*) ((uint8_t*)new_alloc + sizeof(memblk_allocated_t) + final_padding_payload + new_alloc->size);
        new_free->size = free_block_size - (new_alloc->size + sizeof(memblk_allocated_t) + final_padding_header + final_padding_payload);
        
        if(parent_free_block == NULL){
            first_free = new_free;
        }
        else {
            parent_free_block->next = new_free;
        }
        new_free->next = old_next;
        next_fit_current_block = new_free;

        result = new_alloc->payload;
    }
    else {
        //we keep the entire block for ourselves
        if(parent_free_block != NULL){
            parent_free_block->next = old_next;
        }
        else{
            first_free = old_next;
        }
        new_alloc = (memblk_allocated_t*) ((uint8_t*)new_alloc + final_padding_header);
        new_alloc->size =  total_free_block_size - sizeof(memblk_allocated_t); 
        new_alloc->free = 0;
        new_alloc->padding_header = final_padding_header;
        new_alloc->payload = (uint64_t*)((uint8_t*)new_alloc + sizeof(memblk_allocated_t) + final_padding_payload);
        uint8_t* store_padding_payload = (uint8_t*) new_alloc->payload - sizeof(uint8_t);
        *store_padding_payload = final_padding_payload;
        next_fit_current_block = old_next;
        result = new_alloc->payload;
    } 
 
    print_alloc_info((uint64_t*) result, size);
    return result;    
}

void memory_free(void *p)
{
    
    print_free_info(p);

    if(p == NULL){
        debug_printf("Pointer argument is NULL at %s \n", __FUNCTION__); //added this base case
        exit(0);
    }

    uint8_t* payload_padding_info = (uint8_t*) p - sizeof(uint8_t);

    memblk_allocated_t *full_alloc_block = (memblk_allocated_t*)((uint8_t*)p - *payload_padding_info - sizeof(memblk_allocated_t)); 

    if(full_alloc_block->free) {
        debug_printf("Called free on a free block addr at %s\n",__FUNCTION__);
        exit(0);
    }

    full_alloc_block->free=1;
    
    memblk_free_t *free_block=(memblk_free_t *)((uint8_t*) full_alloc_block - full_alloc_block->padding_header);
    
    free_block->size = full_alloc_block->padding_header + sizeof(memblk_allocated_t) + *payload_padding_info + full_alloc_block->size - sizeof(memblk_free_t);

    if(first_free == NULL){ //added this condition in case the heap is full so we don't have to follow with the rest of the code
        first_free = free_block;
        free_block->next = NULL;
        return;
    }

    memblk_free_t *left = NULL;
    memblk_free_t *right = first_free;

    while (right != NULL && free_block >= right) { 
        left = right;
        right = right->next;
    }

    free_block->next=right;

    if (left == NULL)
    {
        first_free=free_block;
    }
    else{
        left->next=free_block;
    }
    
    if(left!=NULL){
        memblk_free_t *end_left_block=(memblk_free_t*)((uint8_t*)left + sizeof(memblk_free_t) + left->size); 
        if((uint8_t*)end_left_block == (uint8_t*)free_block){
            left->size+=free_block->size+sizeof(memblk_free_t);
            left->next=free_block->next;
            free_block=left;
        }
    }
    while(free_block->next!=NULL && ((memblk_free_t*)((uint8_t*)free_block + sizeof(memblk_free_t) + free_block->size)  == free_block->next)){
        free_block->size+=free_block->next->size+sizeof(memblk_free_t);
        free_block->next=free_block->next->next;
    }
    printf("Free list after free:\n");
    for (memblk_free_t *cur = first_free; cur; cur = cur->next) {
        printf("  block @%p, size=%u, next=%p\n", (void*)cur, cur->size, (void*)cur->next);
    }
}

size_t memory_get_allocated_block_size(void *addr)
{
    if(addr == NULL){
        debug_printf("Trying to access address NULL at %s \n",__FUNCTION__);
        exit(0);
    }

    uint8_t* payload_padding_info = (uint8_t*) addr - sizeof(uint8_t);   
    memblk_allocated_t *full_alloc_block = (memblk_allocated_t*)((uint8_t*)addr - *payload_padding_info - sizeof(memblk_allocated_t)); 

    return full_alloc_block->size;
}

    
void print_mem_state(void){ // ONLY WORKS WHEN MEM ALIGN = 1 !
    fprintf(stdout, "[");
    size_t iter = 0;
    uint64_t* p = heap_begin;
    memblk_free_t* first_free_copy = first_free; //again, we do not want to modify the global pointer
    size_t end = MEM_POOL_SIZE;
    while(iter < end){
        if((uint64_t*)first_free_copy == p){ //it's a free block
            for(int i = 0; i< sizeof(memblk_free_t) + first_free_copy->size; i++){
                fprintf(stdout, ".");
            }
            p = (uint64_t*)((uint8_t*)p + sizeof(memblk_free_t) + first_free_copy->size);
            iter += sizeof(memblk_free_t) + first_free_copy->size;
            first_free_copy = first_free_copy->next;
        }
        else { //it's an alloc block
            memblk_allocated_t* alloc = (memblk_allocated_t*) p;
            for(int i = 0; i< sizeof(memblk_allocated_t) + alloc->size; i++){
                fprintf(stdout, "X");
            }
            p = (uint64_t*)((uint8_t*)p + sizeof(memblk_allocated_t) + alloc->size);
            iter += sizeof(memblk_allocated_t) + alloc->size;
        }
    }
    fprintf(stdout, "]\n");
}


void print_info(void) {
    fprintf(stderr, "Memory : [%lu %lu] (%lu bytes)\n", (long unsigned int) heap_begin, (long unsigned int) ((char*)heap_begin+MEM_POOL_SIZE), (long unsigned int) (MEM_POOL_SIZE));
}

void print_free_info(void *addr){
    if(addr){
        fprintf(stderr, "FREE  at : %lu \n", ULONG((char*)addr - (char*)heap_begin));
    }
    else{
        fprintf(stderr, "FREE  at : %lu \n", ULONG(0));
    }
    
}

void print_alloc_info(void *addr, int requested_size){
  if(addr){
    fprintf(stderr, "ALLOC at : %lu (%d byte(s))\n", 
	    ULONG((char*)addr - (char*)heap_begin), requested_size);
  }
  else{
    fprintf(stderr, "Warning, system is out of memory\n"); 
  }
}

void print_alloc_error(int requested_size) 
{
    fprintf(stderr, "ALLOC error : can't allocate %d bytes\n", requested_size);
}


#ifdef MAIN
int main(int argc, char **argv){

  /* The main can be changed, it is *not* involved in tests */
  memory_init();
  print_info();
  int i ; 
  for( i = 0; i < 10; i++){
    char *b = memory_alloc(rand()%8);
    memory_free(b);
  }

  char * a = memory_alloc(15);
  memory_free(a);


  a = memory_alloc(10);
  memory_free(a);

  fprintf(stderr,"%lu\n",(long unsigned int) (memory_alloc(9)));
  return EXIT_SUCCESS;
}
#endif 
