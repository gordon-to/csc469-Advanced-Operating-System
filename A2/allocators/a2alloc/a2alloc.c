#include <stdlib.h>
#include "memlib.h"
#include "malloc.h"

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
#define SUPERBLOCK_SIZE = 4096;					// super block size (S)
static const size_t block_sizes[9] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048};

// Fullness binning
#define FULLNESS_DENOMINATOR = 4;				// Amount to divide S for each fullness bin, and will also act as empty fraction (f)
#define NUM_BINS = FULLNESS_DENOMINATOR + 2;	// Empty, 1-25%, 26-50%, 51-75%, 76-99%, Full

// Typedefs for all the necessary memory objects
typedef unsigned long vaddr_t;

/* Instead of including metadata with each superblock, use metadata objects that
   point to the address of a superblock to ensure contiguous 4KB superblocks. */
typedef struct {
	vaddr_t superblock;			// Address of the actual contiguous superblock
	size_t block_size;			// Block size class (b)
	char block_map[8];			// 512-bit char bitmap indicating which blocks are being used
	int used_blocks;			// A count of the currently used blocks (for fullness)
	void* prev;					// Pointer to previous meta block
	void* next;					// Pointer to next meta block
} sb_meta;

typedef struct {
	pthread_mutex_t lock;
	sb_meta* meta_first;		// Pointer to the first superblock meta object
	sb_meta* bin_first[];		// Array of pointers to first superblock in each bin
} heap;


/*******************************
	FUNCTIONS START
*******************************/
void *malloc_large() {
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
			// make an sb_meta object for it by looking for a free space in meta_first (for heap i)
				// if there's no free space, need to allocate new superblock for meta objects
			// set it as heap i's bin_first[0]
			// remember to set the metadata accordingly and adjust the pointer for the former bin_first 
		// else
			// set the super block as heap i's bin_first[0]
			// remember to set the metadata accordingly and adjust the pointer for the former bin_first
	
	// unlock heap i
	// change block_map and used_blocks accordinly and
	// return one of the free blocks
	
	return NULL;
}

void mm_free(void *ptr) {
	(void)ptr; /* Avoid warning about unused variable */
}


int mm_init(void) {
	// use mem_init to initialize
	// create an array containing a heap for each thread
	// for each heap
		// initialize bin_first to null pointers of length NUM_BINS (superblocks will only be added to heaps using these bins)
		// allocate a superblock and then point meta_first to that superblock (which points to itself)
		// set block_size = 64, block_map[0] to 0x00000001, and rest of block_map to 0
		// set prev, next to NULL
	
		// consider having a bin for each block size * NUM_BINS
	
	// we should now have 9 heaps, each with empty bins and a pointer to their first metablock
	
	return 0;
}
