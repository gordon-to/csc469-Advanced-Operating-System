#include <stdlib.h>
#include <string.h>
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
	char block_map[8];			// 512-bit char bitmap indicating which blocks are being used
	int used_blocks;			// A count of the currently used blocks (for fullness)
	void* prev;					// Pointer to previous meta block
	void* next;					// Pointer to next meta block
} superblock;

typedef struct {
	pthread_mutex_t lock;
	superblock* bin_first[];	// Array of pointers to first superblock in each bin
} heap;

// pointer to hold all heaps
heap * heap_table;

/*******************************
	FUNCTIONS START
*******************************/
void *malloc_large() {
	return NULL;
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


/****** MALLOC FUNCTIONS ******/
void *mm_malloc(size_t sz) {
	(void)sz; /* Avoid warning about unused variable */
	
	// if sz > S/2
		// use malloc_large()
	
	// i = thread_id
	// lock heap i
	// find a fairly full superblock that can fit request size (will usually be bin 5)
		// can do this by iterating down from bin 5 to 0
	
	// if there's no space
		// check heap 0's bin 0 (empty bin)
		// check heap 0's bin 1 (25% bin)
	
		// if there's no superblock
			// call mem_sbrk
		
		// call new_superblock()
		// set the superblock as heap i's bin_first[0]
		// remember to adjust the pointer for the former bin_first
		
	
	// unlock heap i
	// change block_map and used_blocks accordinly and
	// return one of the free blocks
	
	return NULL;
}

void mm_free(void *ptr) {
	(void)ptr; /* Avoid warning about unused variable */
}

void init_sb_meta(superblock* new_sb_meta) {
	new_sb_meta->block_size = SB_SIZE - sizeof(superblock);
	mem_set(&new_sb_meta->block_map, 0, 64);
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
		int i;
		for (i = 0; i <= num_cpu; i++) {
			curr_heap = heap_table + i;
			pthread_mutex_init(&curr_heap->lock, NULL);
			mem_set(&curr_heap->bin_first, NULL, sizeof(superblock *) * num_cpu);
		}
	}
	// use mem_init to initialize
	// create an array containing a heap for each thread
	// for each heap, initialize bin_first to null pointers of length NUM_BINS (superblocks will only be added to heaps using these bins)
	// (there's no longer a need to allocate a first superblock for meta_first)
	
	// consider having a bin for each block size * NUM_BINS
	
	// we should now have 9 heaps, each with empty bins and a pointer to their first metablock
	
	return 0;
}
