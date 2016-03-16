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
	return NULL;
}

void mm_free(void *ptr) {
	(void)ptr; /* Avoid warning about unused variable */
}


int mm_init(void) {
	return 0;
}
