//
// Created by 陈彦骏 on 2019-09-03.
//

#ifndef MYTINYSTL_ALLOC_H
#define MYTINYSTL_ALLOC_H

#include <new>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace mystl {

    union FreeList {
        union FreeList* next;
        char data[1];
    };

    enum {
        EAlign128 = 8,
        EAlign256 = 1,
        EAlign512 = 32,
        EAlign1024 = 64,
        EAlign2048 = 128,
        EAlign4096 = 256
    };

    enum {
        ESmallObjectBytes = 4096
    };

    enum {
        EFreeListsNumber = 56
    };

    class alloc {
    public:
        static void* allocate(size_t n);
        static void deallocate(void* p, size_t n);
        static void* reallocate(void* p, size_t old_size, size_t new_size);

    private:
        static char* start_free;
        static char* end_free;
        static size_t heap_size;
        static FreeList* free_list[EFreeListsNumber];

        static size_t M_align(size_t bytes);
        static size_t M_round_up(size_t bytes);
        static size_t M_freelist_index(size_t bytes);
        static void* M_refill(size_t n);
        static char* M_chunk_alloc(size_t n, size_t &nobjs);

    };

    char* alloc::start_free = nullptr;
    char* alloc::end_free = nullptr;
    size_t alloc::heap_size = 0;

    FreeList* alloc::free_list[EFreeListsNumber] = {
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
    };

    inline void* alloc::allocate(size_t n)
    {
        FreeList* my_free_list;
        FreeList* result;
        if(n > static_cast<size_t>(ESmallObjectBytes))
            return std::malloc(n);
        my_free_list = free_list[M_freelist_index(n)];
        result = my_free_list;
        if(result == nullptr) {
            void* r = M_refill(M_round_up(n));
            return r;
        }
        my_free_list = result->next;
        return result;
    }

    inline void alloc::deallocate(void* p, size_t n)
    {
        if(n > static_cast<size_t>(ESmallObjectBytes)) {
            std::free(p);
            return;
        }
        FreeList* q = reinterpret_cast<FreeList*>(p);
        FreeList* my_free_list;
        my_free_list = free_list[M_freelist_index(n)];
        q->next = my_free_list;
        my_free_list = q;
    }

    inline void* alloc::reallocate(void* p, size_t old_size, size_t new_size)
    {
        deallocate(p, old_size);
        p = allocate(new_size);
        return p;
    }

    size_t alloc::M_align(size_t bytes)
    {
        if(bytes <= 512)
            return bytes <= 256 ? (bytes <= 128 ? EAlign128 : EAlign256) : EAlign512;
        return bytes <= 2048 ? (bytes <= 1024 ? EAlign1024 : EAlign2048) : EAlign4096;
    }

    size_t alloc::M_round_up(size_t bytes)
    {
        return ((bytes + M_align(bytes) - 1) & ~(M_align(bytes) - 1));
    }

    size_t alloc::M_freelist_index(size_t bytes)
    {
        if (bytes <= 512)
        {
            return bytes <= 256
                   ? bytes <= 128
                     ? ((bytes + EAlign128 - 1) / EAlign128 - 1)
                     : (15 + (bytes + EAlign256 - 129) / EAlign256)
                   : (23 + (bytes + EAlign512 - 257) / EAlign512);
        }
        return bytes <= 2048
               ? bytes <= 1024
                 ? (31 + (bytes + EAlign1024 - 513) / EAlign1024)
                 : (39 + (bytes + EAlign2048 - 1025) / EAlign2048)
               : (47 + (bytes + EAlign4096 - 2049) / EAlign4096);
    }


    void* alloc::M_refill(size_t n)
    {
        size_t nblock = 10;
        char* c = M_chunk_alloc(n, nblock);
        FreeList* result;
        FreeList* my_free_list, *curr, *next;
        if(nblock == 1)
            return c;
        my_free_list = free_list[M_freelist_index(n)];
        result = reinterpret_cast<FreeList*>(c);
        my_free_list = next = reinterpret_cast<FreeList*>(c + n);
        for(size_t i = 1; ; ++i) {
            curr = next;
            next = reinterpret_cast<FreeList*>(reinterpret_cast<char*>(curr) + n);
            if(nblock - 1 == i) {
                curr->next = nullptr;
                break;
            } else
                curr->next = next;
        }
        return result;
    }


    char* alloc::M_chunk_alloc(size_t size, size_t &nobjs)
    {
        char* result;
        size_t need_bytes = size * nobjs;
        size_t pool_bytes = end_free - start_free;
        if(pool_bytes >= need_bytes) {
            result = start_free;
            start_free += need_bytes;
            return result;
        }

        else if(pool_bytes >= size) {
            nobjs = pool_bytes / size;
            need_bytes = size * nobjs;
            result = start_free;
            start_free += need_bytes;
            return result;
        }

        else {
            if(pool_bytes > 0) {
                FreeList* my_free_list = free_list[M_freelist_index(pool_bytes)];
                reinterpret_cast<FreeList*>(start_free)->next = my_free_list;
                my_free_list->next = reinterpret_cast<FreeList*>(start_free);
            }
            size_t bytes_to_get = (need_bytes << 1) + M_round_up(heap_size >> 4);
            start_free = reinterpret_cast<char*>(std::malloc(bytes_to_get));
            if(start_free == nullptr) {
                FreeList* my_free_list, *p;
                for(size_t i = size; i <= ESmallObjectBytes; i += M_align(i)) {
                    my_free_list = free_list[M_freelist_index(i)];
                    p = my_free_list;
                    if(p != nullptr) {
                        my_free_list = p->next;
                        start_free = reinterpret_cast<char*>(p);
                        end_free = start_free + i;
                        return M_chunk_alloc(size, nobjs);
                    }
                    std::printf("out of memory");
                    end_free = nullptr;
                    throw std::bad_alloc();
                }
            }
            end_free = start_free + bytes_to_get;
            heap_size += bytes_to_get;
            return M_chunk_alloc(size, nobjs);
        }
    }

}

#endif //MYTINYSTL_ALLOC_H
