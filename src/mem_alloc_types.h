#ifndef   	_MEM_ALLOC_TYPES_H_
#define   	_MEM_ALLOC_TYPES_H_
#include <stdint.h>

struct memblk_free{
    uint32_t size;
    struct memblk_free *next;
}; 
typedef struct memblk_free memblk_free_t; 

struct memblk_allocated{
    uint32_t size;
    bool free;
    uint8_t padding_header;
    uint64_t *payload;
};
typedef struct memblk_allocated memblk_allocated_t;

#endif
