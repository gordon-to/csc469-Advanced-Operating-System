#define _GNU_SOURCE
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "memlib.h"
#include "malloc.h"
#include "mm_thread.h"

name_t myname = {
	/* team name to be displayed on webpage */
	"tbd",
	/* Full name of first team member */
	"Harris Fok",
	/* Email address of first team member */
	"harris.fok@mail.utoronto.ca"
	/* Full name of second team member */
	"Yu-Ching Chen",
	/* Email address of second team member */
	"yuch.chen@mail.utoronto.ca"
};

/*******************************
	GLOBALS START
*******************************/

// Block sizes
#define SB_SIZE 4096					// super block size (S)
static const size_t block_sizes[9] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048};

// Fullness binning
#define FULLNESS_DENOMINATOR 4				// Amount to divide S for each fullness bin, and will also act as empty fraction (f)
#define NUM_BINS FULLNESS_DENOMINATOR + 2	// Empty, 1-25%, 26-50%, 51-75%, 76-99%, Full

// Typedefs for all the necessary memory objects
typedef unsigned long vaddr_t;

typedef struct {
	size_t block_size;			// Block size class (b)
	char block_map[64];			// 512-bit char bitmap indicating which blocks are being used
	int used_blocks;			// A count of the currently used blocks (for fullness)
	void* prev;					// Pointer to previous meta block
	void* next;					// Pointer to next meta block
} superblock;

typedef struct {
	int num_pages;
	void * next;
} node;

typedef struct {
	pthread_mutex_t lock;
	superblock* bin_first[];	// Array of pointers to first superblock in each bin
} heap;

// pointer to hold all heaps
heap * heap_table;
void * large_malloc_table;

/*******************************
	FUNCTIONS START
*******************************/
void *malloc_large(size_t sz, int cpu_id) {
	int pg_size = mem_pagesize();
	int num_pages = ceil((sz+ sizeof(node))/pg_size);
	node * lm_cpu = large_malloc_table + cpu_id;

	while (lm_cpu->next != NULL){
		lm_cpu = lm_cpu->next;
	}

	void * tmp = mem_sbrk(num_pages * pg_size);	
	lm_cpu->next = tmp;
	lm_cpu->num_pages = num_pages;

	return (lm_cpu->next + sizeof(node));
}

/* Creates a superblock at the given address and returns the pointer to it. */
superblock *new_superblock(void* ptr, size_t block_size) {
	// We will run into an issue with reserving space for the metadata at the
	// beginning of the superblock.
	// The way I think we can handle this is:
	//	- If the block size is less/equal to 2 * sizeof(superblock), then
	//		the first x blocks taken up by the superblock will just always be
	//		1's in the block_map (permanently taken)
	//	- Otherwise, if block size is more than 2 * sizeof(superblock),
	//		leave the first block as empty (0), but treat it as a special case
	//		when alloc'ing blocks.
	
	// Then, when we're searching for free blocks during malloc, we can still
	// use the first block, and it'll be the right size so we only have to
	// check superblocks with the same block_size, the space will just be 32
	// bytes smaller
	
	// tl;dr use the bullet point rules above

	return NULL;
}

/* Given a superblock's non-full block_map, look for a free block */
int find_block(char* block_map, size_t block_size) {
	// Need to be careful with block_size > 512
	float num_chars = (float)SB_SIZE / (block_size * 8.f);
	int j, k;
	
	if(num_chars < 1) {
		// block_size was >512, therefore only check the first few bits
		for(k = 0; k < round(8 * num_chars); k++) {
			if(!block_map[0] % (int)pow(2, k)) {
				// Block at bit k is free
				return k;
			}
		}
	}
	
	// Else, iterate through as many block_maps as necessary
	for(j = 0; j < num_chars; j++) {
		if(block_map[j] < 256) {
			for(k = 0; k < 8; k++) {
				if(!block_map[j] % (int)pow(2, k)) {
					// Block at bit k is free
					return j * 8 + k;
				}
			}
		}
	}
	
	// If we somehow fell out of all of that, means that block_map is full
	return -1;
}



int get_cpuid(int tid) {
	int cpu_id = -1;
	cpu_set_t mask;
	CPU_ZERO(&mask);
	if (sched_getaffinity(tid, sizeof(cpu_set_t), &mask) != 0) {
		perror("sched_getaffinity failed");
	} else {
		int i;
		for (i = 0; i < getNumProcessors(); i ++){
			if (CPU_ISSET(i, &mask)) {
				cpu_id = i;
				break;
			}
		}
	}
	if (cpu_id < 0) {
		perror("Unable to get CPUID");
	}
	return cpu_id;
}

/****** MALLOC FUNCTIONS ******/
void *mm_malloc(size_t sz) {
	int tid = getTID();
	int cpu_id = get_cpuid(tid);
	if(sz > SB_SIZE / 2) {
		// Request size is too large, fall back to generic allocation
		return malloc_large(sz, cpu_id);
	}
	
	int j;
	superblock* target_sb = NULL;
	char use_first = 0;
	size_t size_class;
	
	// Get size class
	for(j = 0; j < 9; j++) {
		if(sz <= block_sizes[j]) {
			size_class = block_sizes[j];
		}
	}
	// pthread_mutex_lock(&heap_table[cpu_id+1].lock);
	
	// Find a fairly full superblock that can fit request size (usually bin 5)
	for(j = NUM_BINS - 1; j >= 0; j--) {
		superblock* sb = heap_table[cpu_id+1].bin_first[j];
		
		// Searching bin for superblock with space
		while(sb) {
			if(size_class == sb->block_size) {
				// Special case check for first block (shared with metadata)
				int cant_use_first = 0;
				if(sz > size_class - sizeof(superblock) &&
													!(sb->block_map[0] % 2)) {
					cant_use_first = 1;
				} else {
					use_first = 1;
					target_sb = sb;
					break;
				}
				
				// Now do normal check
				if(sb->used_blocks + cant_use_first < SB_SIZE / sb->block_size) {
					// Found a superblock with space
					target_sb = sb;
					break;
				}
			}
			
			sb = sb->next;
		}
		
		if(target_sb) {
			break;
		}
	}
	
	// if there's no space
		// check heap 0's bin 0 (empty bin)
		// check heap 0's bin 1 (25% bin)
	
		// if there's no superblock
			// call mem_sbrk
		
		// call new_superblock()
		// set the superblock as heap i's bin_first[0]
		// remember to adjust the pointer for the former bin_first
		
	// Now that we have our superblock, get a free block
	void* block;
	if(use_first) {
		// Special case for first block
		block = (char*)target_sb + sizeof(superblock);
		target_sb->block_map[0]++;
		target_sb->used_blocks++;
	} else {
		// Check the block_maps for free blocks
		int block_id = find_block(target_sb->block_map, size_class);
		block = (char*)target_sb + block_id * size_class;
		// printf("target_sb + (block_id * size_class) = block\n");
		// printf("%p + %d = %p\n", target_sb, block_id * size_class, block);
		
		// Change block map and used_blocks
		target_sb->block_map[block_id/8] += pow(2, block_id % 8);
		target_sb->used_blocks++;
	}
	
	// pthread_mutex_unlock(&heap_table[cpu_id+1].lock);
	return block;
}

void mm_free(void *ptr) {
	(void)ptr; /* Avoid warning about unused variable */
}

void init_sb_meta(superblock* new_sb_meta) {
	new_sb_meta->block_size = SB_SIZE - sizeof(superblock);
	memset(&new_sb_meta->block_map, 0, 64);
	new_sb_meta->used_blocks = 0;
	new_sb_meta->prev = NULL;
	new_sb_meta->next = NULL;
}


int mm_init(void) {

	if (!mem_init()) {
		// need to reflect changes in this code (from metadata struct design to embedded metadata design)
		int num_cpu = getNumProcessors();
		heap_table = (heap *) dseg_lo;
		heap* curr_heap;
		superblock * bin_start = (superblock *) (heap_table + num_cpu);
		int i;
		for (i = 0; i <= num_cpu; i++) {
			curr_heap = heap_table + i;
			curr_heap->bin_first[0] = bin_start + (i * NUM_BINS);
			pthread_mutex_init(&curr_heap->lock, NULL);
			memset(curr_heap->bin_first[0], 0, sizeof(superblock *) * num_cpu);
		}
		large_malloc_table = bin_start + ((num_cpu+1) * NUM_BINS);
		memset(large_malloc_table, 0, sizeof(node) * num_cpu);

	}
	// use mem_init to initialize
	// create an array containing a heap for each thread
	// for each heap, initialize bin_first to null pointers of length NUM_BINS (superblocks will only be added to heaps using these bins)
	// (there's no longer a need to allocate a first superblock for meta_first)
	
	// consider having a bin for each block size * NUM_BINS
	
	// we should now have 9 heaps, each with emptyins and a pointer to their first metablock
	
	return 0;
}
