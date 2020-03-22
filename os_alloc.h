#ifndef BEEBJIT_OS_ALLOC_H
#define BEEBJIT_OS_ALLOC_H

#include <stddef.h>
#include <stdint.h>

void* os_alloc_aligned(size_t alignment, size_t size);

intptr_t os_alloc_get_memory_handle(size_t size);
void os_alloc_get_mapping_from_handle_replace(intptr_t handle,
                                              void* p_addr,
                                              size_t size);
void* os_alloc_get_guarded_mapping_from_handle(intptr_t handle,
                                               void* p_addr,
                                               size_t size);

void* os_alloc_get_guarded_mapping(void* p_addr, size_t size);
void os_alloc_free_guarded_mapping(void* p_addr, size_t size);

void os_alloc_get_anonymous_mapping_replace(void* p_addr, size_t size);

void os_alloc_make_mapping_read_only(void* p_addr, size_t size);
void os_alloc_make_mapping_read_write(void* p_addr, size_t size);
void os_alloc_make_mapping_read_write_exec(void* p_addr, size_t size);
void os_alloc_make_mapping_none(void* p_addr, size_t size);

#endif /* BEEBJIT_OS_ALLOC_H */
