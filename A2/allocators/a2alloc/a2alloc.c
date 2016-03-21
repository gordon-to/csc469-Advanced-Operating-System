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

typedef struct superblock superblock;
struct superblock {
	size_t block_size;			// Block size class (b)
	char block_map[64];			// 512-bit char bitmap indicating which blocks are being used
	int used_blocks;			// A count of the currently used blocks (for fullness)
	superblock* prev;					// Pointer to previous meta block
	superblock* next;					// Pointer to next meta block
};

typedef struct {
	pthread_mutex_t lock;
	// 2D Array of pointers to first superblock in each bin
	// The first range specifies block size class (the ones from 8 - 2048)
	// The second range specifies fullness (from 0 - 100%)
	superblock* bin_first[9][NUM_BINS];
} heap;

// pointer to hold all heaps
heap * heap_table;

/*******************************
	FUNCTIONS START
*******************************/
void *malloc_large(size_t sz) {
	(void)sz;
	return NULL;
}

/* Creates a superblock at the given address and returns the pointer to it. */
superblock *new_superblock(void* ptr, size_t block_size) {
	superblock* new_sb = (superblock*)ptr;
	
	new_sb->block_size = block_size;
	memset(&new_sb->block_map, 0, 64);
	new_sb->used_blocks = 0;
	new_sb->prev = NULL;
	new_sb->next = NULL;

	return new_sb;
}

/* Transfers a superblock from one bin to the front of the specified new bin.
   Assumes global heap is locked. Returns error status. */
int transfer_bins(superblock* sb, int* orig, int* dest) {
	// Origin and destination are integer arrays that go by the following:
	// [heap_id, size class, fullness]
	
	// First remove the sb from its original bin
	if(sb->next) {
		sb->next->prev = sb->prev;
	}
	if(sb->prev) {
		sb->prev->next = sb->next;
	} else {
		heap_table[orig[0]].bin_first[orig[1]][orig[2]] = sb->next;
	}
	
	// Then insert it into the front of the new bin
	if(heap_table[dest[0]].bin_first[dest[1]][dest[2]]) {
		heap_table[dest[0]].bin_first[dest[1]][dest[2]]->prev = sb;
	}
	sb->next = heap_table[dest[0]].bin_first[dest[1]][dest[2]];
	sb->prev = NULL;
	heap_table[dest[0]].bin_first[dest[1]][dest[2]] = sb;
	
	return 0;
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
	if(sz > SB_SIZE / 2) {
		// Request size is too large, fall back to generic allocation
		return malloc_large(sz);
	}
	
	int i, old_fullness, size_id;
	int tid = getTID();
	int cpu_id = get_cpuid(tid);
	superblock* target_sb = NULL;
	char use_first = 0;
	size_t size_class = 8;
	
	// Get size class
	for(size_id = 0; size_id < 9; size_id++) {
		if(sz <= block_sizes[size_id]) {
			size_class = block_sizes[size_id];
		}
	}
	// pthread_mutex_lock(&heap_table[cpu_id+1].lock);
	
	// Find a fairly full superblock with the appropriate block_size
	// Go from nearly full bin to empty bin
	for(i = NUM_BINS - 1; i >= 0; i--) {
		superblock* sb = heap_table[cpu_id+1].bin_first[size_id][i];
		
		// Searching bin for superblock with space
		while(sb) {
			// Special case check for first block (shared with metadata)
			int cant_use_first = 0;
			if(!(sb->block_map[0] % 2) && sz > size_class - sizeof(superblock)) {
				// If the first block is "free" but the request size doesn't
				// fit in that space, then we have to consider the block as 
				// unusable for this case.
				cant_use_first = 1;
			} else {
				use_first = 1;
				target_sb = sb;
				old_fullness = i;
				break;
			}
			
			// Now do normal check
			if(sb->used_blocks + cant_use_first < SB_SIZE / size_class) {
				// Found a superblock with space
				target_sb = sb;
				old_fullness = i;
				break;
			}
			
			sb = sb->next;
		}
		
		if(target_sb) {
			break;
		}
	}
	
	// If there were no available superblocks with the right block_size, then
	// we need to get a new one
	if(!target_sb) {
		int origin[3] = {0, 0, 0};
		pthread_mutex_lock(&heap_table[0].lock);
		
		// check all of heap 0's empty bins
		for(i = 0; i < 9; i++) {
			if(heap_table[0].bin_first[i][0]) {
				target_sb = heap_table[0].bin_first[i][0];
				origin[0] = 0;
				origin[1] = i;
				origin[2] = 0;
				
				// Reset the block_size
				target_sb->block_size = size_class;
				// Also reset the block_map and used_blocks just in case
				memset(&target_sb->block_map, 0, 64);
				target_sb->used_blocks = 0;
			}
		}
		
		// check heap 0's bin 1 for size_id (25% bin)
		if(!target_sb && heap_table[0].bin_first[size_id][1]) {
			target_sb = heap_table[0].bin_first[size_id][1];
			origin[0] = 0;
			origin[1] = size_id;
			origin[2] = 1;
		}
		
		// If there literally were no blocks, we'll have to sbrk for one
		if(!target_sb) {
			void* tmp = mem_sbrk(mem_pagesize());
			if(!tmp) {
				// No more space!
				return NULL;
			}
			
			// Initialize the superblock
			target_sb = new_superblock(tmp, size_class);
			// For compatibility, we'll slot it into the global heap temporarily
			heap_table[0].bin_first[size_class][0] = target_sb;
			origin[0] = 0;
			origin[1] = size_id;
			origin[2] = 0;
		}
		
		// Move the superblock to the appropriate heap's bin_first
		int destination[3] = {cpu_id+1, size_id, 1};
		transfer_bins(target_sb, origin, destination);
		old_fullness = 1;
		pthread_mutex_unlock(&heap_table[0].lock);
	}
		
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
	
	// If the bin has changed fullness, we'll need to move it accordingly
	float new_percent = target_sb->used_blocks / (SB_SIZE / size_class);
	int new_fullness = (int)(new_percent * FULLNESS_DENOMINATOR) + 1;
	if(new_fullness != old_fullness) {
		int origin[3] = {cpu_id+1, size_class, old_fullness};
		int destination[3] = {cpu_id+1, size_class, new_fullness};
		transfer_bins(target_sb, origin, destination);
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
			curr_heap->bin_first[0][0] = bin_start + (i * NUM_BINS);
			pthread_mutex_init(&curr_heap->lock, NULL);
			memset(curr_heap->bin_first[0], 0, sizeof(superblock *) * num_cpu);
		}
	}
	// use mem_init to initialize
	// create an array containing a heap for each thread
	// for each heap, initialize bin_first to null pointers of length NUM_BINS (superblocks will only be added to heaps using these bins)
	// (there's no longer a need to allocate a first superblock for meta_first)
	
	// consider having a bin for each block size * NUM_BINS
	
	// we should now have 9 heaps, each with just empty bins
	
	return 0;
}
