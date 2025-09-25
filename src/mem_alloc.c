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

//TODO: usage of the byte ->free, magic number logic, First Fit already implemented, print mem state,
//possible optimisations?


/* pointer to the beginning of the memory region to manage */
void *heap_begin;

/* Pointer to the first free block in the heap */
memblk_free_t *first_free; 


#define ULONG(x)((long unsigned int)(x))


#if defined(FIRST_FIT)
 
/* TODO: code specific to the FIRST FIT allocation policy can be
 * inserted here */

/* You can define here functions that will be compiled only if the
 * symbol FIRST_FIT is defined, that is, only if the selected policy
 * is FF */

 void* memory_alloc(size_t size);

#elif defined(BEST_FIT)

/* TODO: code specific to the BEST FIT allocation policy can be
 * inserted here */

#elif defined(NEXT_FIT)

/* TODO: code specific to the NEXT FIT allocation policy can be
 * inserted here */

#endif


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
}

void *memory_alloc(size_t size){ 

    if(size > MEM_POOL_SIZE){ //should never happen
        print_alloc_error(size);
        exit(0);
    }

    memblk_free_t *first_free_copy = first_free; //we don't want to modify the global pointer!!

    if(first_free_copy == NULL) { // not a single free node found..so we terminate here
        print_alloc_error(size);
        exit(0);
    }

    uint64_t *result = NULL;
    uint32_t free_block_size;
    uint32_t total_free_block_size; //including the header
    memblk_allocated_t *new_alloc;
    memblk_free_t *parent_free_block = NULL;
    size_t max_header_size = MAX(sizeof(memblk_free_t), sizeof(memblk_allocated_t));

    while(first_free_copy != NULL){
        free_block_size = first_free_copy->size;
        total_free_block_size = free_block_size + sizeof(memblk_free_t);
        if(total_free_block_size > size + sizeof(memblk_allocated_t)) { //we have sufficient space to allocate mem
            if(total_free_block_size - (size + sizeof(memblk_allocated_t)) >= max_header_size){ //choice between split or not
                // we split..
                memblk_allocated_t* new_alloc = (memblk_allocated_t*) first_free_copy;
                new_alloc->size = size;
                new_alloc->free = 0;
                new_alloc->payload = (uint64_t*)((uint8_t*)new_alloc + sizeof(memblk_allocated_t));

                memblk_free_t* new_free = (memblk_free_t*) ((uint8_t*)new_alloc + sizeof(memblk_allocated_t) + size);
                new_free->size = free_block_size - (sizeof(memblk_allocated_t) + size);
                new_free->next = first_free_copy->next;
                result = new_alloc->payload;
                if(parent_free_block == NULL){
                    first_free = new_free;
                }
                else {
                    parent_free_block->next = new_free;
                }
            }
            else {
                //we keep the entire block for ourselves
                if(parent_free_block != NULL){
                    parent_free_block->next = first_free_copy->next;
                }
                else{
                    first_free = first_free_copy->next;
                }
                new_alloc = (memblk_allocated_t*) first_free_copy;
                new_alloc->size = total_free_block_size - sizeof(memblk_allocated_t);
                new_alloc->free = 0; 
                new_alloc->payload = (uint64_t*)((uint8_t*)new_alloc + sizeof(memblk_allocated_t));
                result = new_alloc->payload;
            }
            break; //we finished our duty and found a free space
        }
        else { // we're searching for the next free block struct and update the parent pointer
            if(first_free_copy->next != NULL){
                parent_free_block = first_free_copy;
                first_free_copy = first_free_copy->next;
            }
            else { // we didn't find a free space for the specific alloc req
                first_free_copy = NULL; // we didn't find a free space for the specific alloc req
                print_alloc_error(size);
                exit(0);
            }
        }
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

    memblk_allocated_t *full_alloc_block = (memblk_allocated_t*)((uint8_t*)p - sizeof(memblk_allocated_t)); // modified here

    if(full_alloc_block->free) {
        debug_printf("Called free on a free block addr at %s\n",__FUNCTION__);
        exit(0);
    }

    full_alloc_block->free=1;
    
    memblk_free_t *free_block=(memblk_free_t *) full_alloc_block;

    free_block->size=full_alloc_block->size;

    if(first_free == NULL){ //added this condition in case the heap is full so we don't have to follow with the rest of the code
        first_free = free_block;
        free_block->next = NULL;
        return;
    }

    memblk_free_t *left = NULL;
    memblk_free_t *right = first_free;

    while (right != NULL && free_block >= right) { //replaced > by >=
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

    if(free_block->next!=NULL){
        memblk_free_t *end_free_block=(memblk_free_t*)((uint8_t*)free_block + sizeof(memblk_free_t) + free_block->size); //modified here

        if(end_free_block==free_block->next){
            free_block->size=free_block->size+free_block->next->size+sizeof(memblk_free_t);
            free_block->next=free_block->next->next; 
        }
    }
    
    if(left!=NULL){
        memblk_free_t *end_left_block=(memblk_free_t*)((uint8_t*)left + sizeof(memblk_free_t) + left->size); //modified here
        if(end_left_block==free_block){
            left->size=free_block->size+left->size+sizeof(memblk_free_t);
            left->next=free_block->next;
            free_block=left;
        }
    }
}

size_t memory_get_allocated_block_size(void *addr)
{
    if(addr == NULL){
        debug_printf("Trying to access address NULL at %s \n",__FUNCTION__);
        exit(0);
    }
    
    memblk_allocated_t *full_alloc_block = (memblk_allocated_t*)((uint8_t*)addr - sizeof(memblk_allocated_t)); 

    return full_alloc_block->size;
}

    
void print_mem_state(void)
{
    /* 
    
    
    */
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
