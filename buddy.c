#include "buddy.h"
#include <stddef.h>
#include <stdlib.h>
#define NULL ((void *)0)

#define PAGE_SIZE (4096)
#define MAX_RANK 16

/* Metadata for each block - only the first page of block has valid allocated info */
typedef struct {
    int rank;
    int allocated;
} page_t;

static page_t *pages;
static int free_counts[MAX_RANK + 1];
static void *memory_base;
static int total_pages;

/* Get page index from address */
static int addr_to_idx(void *addr) {
    if (addr < memory_base) return -1;
    ptrdiff_t offset = (char *)addr - (char *)memory_base;
    if (offset % PAGE_SIZE != 0) return -1;
    int idx = (int)(offset / PAGE_SIZE);
    if (idx < 0 || idx >= total_pages) return -1;
    return idx;
}

int init_page(void *p, int pgcount) {
    memory_base = p;
    total_pages = pgcount;

    pages = (page_t *)malloc(sizeof(page_t) * pgcount);
    if (!pages) return -ENOSPC;

    for (int i = 0; i <= MAX_RANK; i++) {
        free_counts[i] = 0;
    }

    int curr = 0;
    int rem = pgcount;
    while (rem > 0) {
        int r = 1;
        for (int tryp = MAX_RANK; tryp >= 1; tryp--) {
            if ((1 << (tryp - 1)) <= rem) {
                r = tryp;
                break;
            }
        }
        int sz = 1 << (r - 1);
        pages[curr].rank = r;
        pages[curr].allocated = 0;
        free_counts[r]++;
        for (int i = 1; i < sz; i++) {
            pages[curr + i].rank = r;
            pages[curr + i].allocated = 1;
        }
        curr += sz;
        rem -= sz;
    }
    return OK;
}

/* Find a free block of given rank, return its starting index, or -1 if none */
static int find_free(int rank) {
    int step;
    for (int i = 0; i < total_pages; i += step) {
        step = 1 << (pages[i].rank - 1);
        if (pages[i].rank == rank && pages[i].allocated == 0) {
            return i;
        }
    }
    return -1;
}

/* Split a larger block to get a block of target rank */
static int split_recursive(int target) {
    if (target >= MAX_RANK) return -1;

    /* Find larger rank with free block */
    int r;
    for (r = target + 1; r <= MAX_RANK; r++) {
        if (free_counts[r] > 0) break;
    }
    if (r > MAX_RANK) return -1;

    /* Get a free block of rank r */
    int idx = find_free(r);
    if (idx < 0) return -1;

    /* Remove from free count */
    pages[idx].allocated = 1;
    free_counts[r]--;

    int half_sz = 1 << (r - 2);

    /* Split into two blocks of rank r-1 */
    int buddy1 = idx;
    int buddy2 = idx + half_sz;

    pages[buddy1].rank = r - 1;
    pages[buddy1].allocated = 0;
    free_counts[r - 1]++;

    pages[buddy2].rank = r - 1;
    pages[buddy2].allocated = 0;
    free_counts[r - 1]++;

    for (int i = 1; i < half_sz; i++) {
        pages[buddy1 + i].rank = r - 1;
        pages[buddy1 + i].allocated = 1;
        pages[buddy2 + i].rank = r - 1;
        pages[buddy2 + i].allocated = 1;
    }

    if (r - 1 == target) {
        /* We now have a free block, return the lower index */
        return buddy1 < buddy2 ? buddy1 : buddy2;
    } else {
        /* Split again to get smaller */
        return split_recursive(target);
    }
}

void *alloc_pages(int rank) {
    if (rank < 1 || rank > MAX_RANK) {
        return ERR_PTR(-EINVAL);
    }

    int idx;
    if (free_counts[rank] > 0) {
        idx = find_free(rank);
        if (idx >= 0) {
            pages[idx].allocated = 1;
            free_counts[rank]--;
            int sz = 1 << (rank - 1);
            for (int i = 1; i < sz; i++) {
                pages[idx + i].allocated = 1;
            }
            return (void *)((char *)memory_base + idx * PAGE_SIZE);
        }
    }

    idx = split_recursive(rank);
    if (idx < 0) {
        return ERR_PTR(-ENOSPC);
    }

    pages[idx].allocated = 1;
    free_counts[rank]--;
    int sz = 1 << (rank - 1);
    for (int i = 1; i < sz; i++) {
        pages[idx + i].allocated = 1;
    }

    return (void *)((char *)memory_base + idx * PAGE_SIZE);
}

int return_pages(void *p) {
    if (p == NULL) {
        return -EINVAL;
    }

    int idx = addr_to_idx(p);
    if (idx < 0) {
        return -EINVAL;
    }

    if (pages[idx].allocated == 0) {
        return -EINVAL;
    }

    int rank = pages[idx].rank;
    int sz = 1 << (rank - 1);

    /* Mark all pages as free */
    pages[idx].allocated = 0;
    for (int i = 1; i < sz; i++) {
        pages[idx + i].allocated = 0;
    }

    int curr_idx = idx;
    int curr_rank = rank;

    while (curr_rank < MAX_RANK) {
        int curr_sz = 1 << (curr_rank - 1);
        int buddy = curr_idx ^ curr_sz;

        if (buddy < 0 || buddy + curr_sz > total_pages) {
            break;
        }

        if (pages[buddy].allocated != 0 || pages[buddy].rank != curr_rank) {
            break;
        }

        /* Remove buddy from free list */
        free_counts[curr_rank]--;

        /* Coalesce into larger block */
        int merged = curr_idx < buddy ? curr_idx : buddy;
        int merged_rank = curr_rank + 1;
        int merged_sz = 1 << curr_rank;

        pages[buddy].allocated = 1;  /* Mark buddy as used now (it's part of larger block) */
        pages[merged].rank = merged_rank;
        pages[merged].allocated = 0;

        for (int i = 1; i < merged_sz; i++) {
            pages[merged + i].rank = merged_rank;
            pages[merged + i].allocated = 1;
        }

        curr_idx = merged;
        curr_rank = merged_rank;
    }

    /* Add final merged block to free list */
    free_counts[curr_rank]++;

    return OK;
}

int query_ranks(void *p) {
    int idx = addr_to_idx(p);
    if (idx < 0) {
        return -EINVAL;
    }
    return pages[idx].rank;
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > MAX_RANK) {
        return -EINVAL;
    }
    return free_counts[rank];
}
