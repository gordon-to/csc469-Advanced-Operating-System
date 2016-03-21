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
#define SB_SIZE 4096						// super block size (S)
#define K 8									// Minimum superblock threshold (K)
static const size_t block_sizes[9] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048};

// Fullness binning
#define FULLNESS_DENOMINATOR 4				// Amount to divide S for each fullness bin, and will also act as empty fraction (f)
#define NUM_BINS FULLNESS_DENOMINATOR + 2	// Empty, 1-25%, 26-50%, 51-75%, 76-99%, Full

// Typedefs for all the necessary memory objects
typedef unsigned long vaddr_t;

typedef struct superblock superblock;
struct superblock {
	int type;					// Indicator that this is a regular superblock (set to 0)
	size_t block_size;			// Block size class (b)
	char block_map[64];			// 512-bit char bitmap indicating which blocks are being used
	int used_blocks;			// A count of the currently used blocks (for fullness)
	int heap_id;
	superblock* prev;			// Pointer to previous superblock
	superblock* next;			// Pointer to next superblock
};

typedef struct {
	int type;					// Indicator that this is a large block (set to 1)
	int num_pages;
	void * next;
} node;

typedef struct {
	pthread_mutex_t lock;
	// 2D Array of pointers to first superblock in each bin
	// The first range specifies block size class (the ones from 8 - 2048)
	// The second range specifies fullness (from 0 - 100%)
	int allocated;				// Space allocated to the heap in bytes
	int used;					// Space used within the heap in bytes
	superblock* bin_first[9][NUM_BINS];
} heap;

// pointer to hold all heaps
heap ** heap_table;
node * large_malloc_table;

// global locks
pthread_mutex_t sbrk_lock = PTHREAD_MUTEX_INITIALIZER;
/*******************************
	FUNCTIONS START
*******************************/

/* Creates a superblock at the given address and returns the pointer to it.
   ASSUMES the superblock will be going into the global heap, otherwise you are
   required to change the heap_id after. */
superblock *new_superblock(void* ptr, size_t block_size) {
	superblock* new_sb = (superblock*)ptr;
	
	new_sb->type = 0;
	new_sb->block_size = block_size;
	memset(&new_sb->block_map, 0, 64);
	new_sb->used_blocks = 0;
	// For small block size, first blocks are pre-reserved for metadata
	if(block_size < sizeof(superblock) * 2) {
		int blocks_used = ceil((float)sizeof(superblock) / (float)block_size);
		new_sb->used_blocks = blocks_used;
		
		if(blocks_used <= 8) {
			new_sb->block_map[0] = (int)pow(2, blocks_used) - 1;
		} else {
			new_sb->block_map[0] = 255;
			new_sb->block_map[1] = (int)pow(2, blocks_used % 8) - 1;
		}
	}
	
	new_sb->heap_id = 0;
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
		heap_table[orig[0]]->bin_first[orig[1]][orig[2]] = sb->next;
	}
	
	// Then insert it into the front of the new bin
	if(heap_table[dest[0]]->bin_first[dest[1]][dest[2]]) {
		heap_table[dest[0]]->bin_first[dest[1]][dest[2]]->prev = sb;
	}
	sb->next = heap_table[dest[0]]->bin_first[dest[1]][dest[2]];
	sb->prev = NULL;
	heap_table[dest[0]]->bin_first[dest[1]][dest[2]] = sb;
	if(dest[0] != orig[0]) {
		sb->heap_id = dest[0];
	}
	
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

int get_cpuid() {
	int tid = getTID();
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

void *malloc_large(size_t sz, int cpu_id) {
	int pg_size = mem_pagesize();
	int num_pages = (int) ceil((sz + sizeof(node))/pg_size);
	node * lm_cpu = large_malloc_table + cpu_id;
	
	while (lm_cpu->next != NULL){
		lm_cpu = lm_cpu->next;
	}
	
	pthread_mutex_lock(&sbrk_lock);
	node * new = (node*)mem_sbrk(num_pages * pg_size);	
	pthread_mutex_unlock(&sbrk_lock);
	lm_cpu->next = new;
	new->type = 1;
	new->num_pages = num_pages;
	new->next = NULL;

	return (new + sizeof(node));
}

// return 1 if freed, else 0
int free_large(void *ptr, int cpu_id) {
	node * lm_cpu = large_malloc_table + cpu_id;
	while (lm_cpu->next != NULL && lm_cpu->next != (ptr + sizeof(node))){
		lm_cpu = lm_cpu->next;
	}

	if (lm_cpu->next == NULL) return 0;

	int pg_size = mem_pagesize();
	int num_pages = ((node*)lm_cpu->next)->num_pages;
	void* page_starts[num_pages];
	superblock* free_sb[num_pages];
	superblock* prev = NULL;
	int i;
	
	for (i = 0; i < num_pages; i++) {
		page_starts[i] = ((void *) lm_cpu->next) + (pg_size * i);
		
		// Create new superblocks
		free_sb[i] = new_superblock(page_starts[i], 8);		// arbitrary block size
		free_sb[i]->prev = prev;
		if(prev) prev->next = free_sb[i];
		
		prev = free_sb[i];
	}
	
	// Now, put all pointers from array free_sb into global heap
	if(heap_table[0]->bin_first[0][0]) {
		heap_table[0]->bin_first[0][0]->prev = prev;
	}
	prev->next = heap_table[0]->bin_first[0][0];
	heap_table[0]->bin_first[0][0]->next = free_sb[0];

	lm_cpu->next = ((node *) lm_cpu->next)->next;
	return 1;
}


/****** MALLOC FUNCTIONS ******/
void *mm_malloc(size_t sz) {
	int cpu_id = get_cpuid();
	if(sz > SB_SIZE / 2) {
		// Request size is too large, fall back to generic allocation
		return malloc_large(sz, cpu_id);
	}
	
	int i, old_fullness, size_id;
	superblock* target_sb = NULL;
	char use_first = 0;
	size_t size_class = 8;
	
	// Get size class
	for(size_id = 0; size_id < 9; size_id++) {
		if(sz <= block_sizes[size_id]) {
			size_class = block_sizes[size_id];
			break;
		}
	}
	// pthread_mutex_lock(&heap_table[cpu_id+1]->lock);
	
	// Find a fairly full superblock with the appropriate block_size
	// Go from nearly full bin to empty bin
	for(i = NUM_BINS - 1; i >= 0; i--) {
		superblock* sb = heap_table[cpu_id+1]->bin_first[size_id][i];
		
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
		pthread_mutex_lock(&heap_table[0]->lock);
		
		// check all of heap 0's empty bins
		for(i = 0; i < 9; i++) {
			if(heap_table[0]->bin_first[i][0]) {
				target_sb = heap_table[0]->bin_first[i][0];
				origin[0] = 0;
				origin[1] = i;
				origin[2] = 0;
				
				// Reset the superblock (but keep it in the same bin)
				superblock* next = target_sb->next;
				target_sb = new_superblock(target_sb, size_class);
				target_sb->next = next;
				break;
			}
		}
		
		// check heap 0's bin 1 for size_id (25% bin)
		if(!target_sb && heap_table[0]->bin_first[size_id][1]) {
			target_sb = heap_table[0]->bin_first[size_id][1];
			heap_table[0]->used -= target_sb->used_blocks * size_class;
			heap_table[cpu_id+1]->used += target_sb->used_blocks * size_class;
			origin[0] = 0;
			origin[1] = size_id;
			origin[2] = 1;
		}
		
		// If there literally were no blocks, we'll have to sbrk for one
		if(!target_sb) {
			pthread_mutex_lock(&sbrk_lock);
			void* tmp = mem_sbrk(mem_pagesize());
			pthread_mutex_unlock(&sbrk_lock);
			if(!tmp) {
				// No more space!
				return NULL;
			}
			
			// Initialize the superblock
			target_sb = new_superblock(tmp, size_class);
			// For compatibility, we'll slot it into the global heap temporarily
			heap_table[0]->bin_first[size_class][0] = target_sb;
			heap_table[0]->allocated += SB_SIZE;
			origin[0] = 0;
			origin[1] = size_id;
			origin[2] = 0;
		}
		
		// Move the superblock to the appropriate heap's bin_first
		int destination[3] = {cpu_id+1, size_id, 1};
		transfer_bins(target_sb, origin, destination);
		heap_table[0]->allocated -= SB_SIZE;
		heap_table[cpu_id+1]->allocated += SB_SIZE;
		old_fullness = 1;
		pthread_mutex_unlock(&heap_table[0]->lock);
	}
		
	// Now that we have our superblock, get a free block
	void* block;
	if(use_first) {
		// Special case for first block
		block = (char*)target_sb + sizeof(superblock);
		target_sb->block_map[0]++;
	} else {
		// Check the block_maps for free blocks
		int block_id = find_block(target_sb->block_map, size_class);
		block = (char*)target_sb + block_id * size_class;
		// printf("target_sb + (block_id * size_class) = block\n");
		// printf("%p + %d = %p\n", target_sb, block_id * size_class, block);
		
		// Change block map and used_blocks
		target_sb->block_map[block_id/8] += pow(2, block_id % 8);
	}
	target_sb->used_blocks++;
	heap_table[cpu_id+1]->used += size_class;
	
	// If the bin has changed fullness, we'll need to move it accordingly
	float new_percent = target_sb->used_blocks / (SB_SIZE / size_class);
	int new_fullness = (int)(new_percent * FULLNESS_DENOMINATOR) + 1;
	if(new_fullness != old_fullness) {
		int origin[3] = {cpu_id+1, size_id, old_fullness};
		int destination[3] = {cpu_id+1, size_id, new_fullness};
		transfer_bins(target_sb, origin, destination);
	}
	
	// pthread_mutex_unlock(&heap_table[cpu_id+1]->lock);
	return block;
}

void mm_free(void *ptr) {
	int cpu_id = get_cpuid();
	int SID, HID;			// size_id and heap_id
	
	// Move up to page border to read header data
	void* page = (int*)((unsigned long)ptr - ((unsigned long)ptr % mem_pagesize()));
	unsigned long offset = (unsigned long)ptr % mem_pagesize();
	int type = *(int*)page;
	
	// Freeing for large blocks
	if(type) {
		if (free_large(page, cpu_id)) return;
	}
	
	// Get the superblock data and deallocate it from the superblock
	superblock* sb = (superblock*)page;
	HID = sb->heap_id;
	if(HID == 0){
		pthread_mutex_lock(&heap_table[HID]->lock);
	}
	
	// Updating block map
	int block_id = offset / sb->block_size;
	printf("Free: block_id = %d\n", block_id);			// debug
	printf("block_map[block_id/8] = %x\n", sb->block_map[block_id/8]);
	sb->block_map[block_id/8] -= pow(2, block_id % 8);
	
	// Book-keeping variables
	float old_percent = sb->used_blocks / (SB_SIZE / sb->block_size);
	int old_fullness = (int)(old_percent * FULLNESS_DENOMINATOR) + 1;
	for(SID = 0; SID < 9; SID++) {
		if(sb->block_size <= block_sizes[SID]) break;
	}
	sb->used_blocks--;
	heap_table[HID]->used -= sb->block_size;
	
	// Update fullness bins
	float new_percent = sb->used_blocks / (SB_SIZE / sb->block_size);
	int new_fullness = (int)(new_percent * FULLNESS_DENOMINATOR) + 1;
	if(new_fullness != old_fullness) {
		int origin[3] = {HID, SID, old_fullness};
		int destination[3] = {HID, SID, new_fullness};
		transfer_bins(sb, origin, destination);
	}
		
	// If this is global heap, return
	if(HID == 0) {
		pthread_mutex_unlock(&heap_table[0]->lock);
		return;
	}
	
	// If our heap has crossed the emptiness threshold and has more than K
	// superblocks worth of free space, return a mostly if not empty superblock
	// to the global heap
	if(heap_table[HID]->used < heap_table[HID]->allocated / FULLNESS_DENOMINATOR &&
	   heap_table[HID]->used < heap_table[HID]->allocated - K * SB_SIZE) {
	   	int i, j;
		int origin[3] = {0, 0, 0};
		superblock* target_sb = NULL;
		
		// Try to transfer a superblock from the empty bin, then look in the
		// 25% bins if we don't find anything
		for(j = 0; j <= 1; j++) {
			for(i = 0; i < 9; i++) {
				if(heap_table[HID]->bin_first[i][j]) {
					target_sb = heap_table[HID]->bin_first[i][j];
					origin[0] = HID;
					origin[1] = i;
					origin[2] = j;
					break;
				}
			}
			
			// Falling through this means there weren't any completely empty sb
			if(target_sb) {
				break;
			}
		}
		
		// Move the superblock to the global heap
		int destination[3] = {0, origin[1], origin[2]};
		transfer_bins(target_sb, origin, destination);
		int bytes_moved = target_sb->used_blocks * target_sb->block_size;
		heap_table[HID]->used -= bytes_moved;
		heap_table[HID]->allocated -= SB_SIZE;
		heap_table[0]->used += bytes_moved;
		heap_table[0]->allocated += SB_SIZE;
	}
	
	// pthread_mutex_unlock(&heap_table[HID]->lock);
}

int mm_init(void) {
	printf("sizeof(superblock) = %d\n", (int)sizeof(superblock));		// debug
	if (!mem_init()) {
		// need to reflect changes in this code (from metadata struct design to embedded metadata design)
		void * sblock;
		int num_cpu = getNumProcessors();
		int pg_size = mem_pagesize();
		heap_table = (heap **) dseg_lo;
		heap ** c_heap_table;
		heap* curr_heap;
		superblock * bin_start = (superblock *) (heap_table + num_cpu);
		large_malloc_table = (node *) (bin_start + ((num_cpu+1) * NUM_BINS));

		int i;
		for (i = 0; i <= num_cpu; i++) {
			sblock = mem_sbrk(pg_size);
			curr_heap = (heap *) sblock;
			c_heap_table = heap_table + i;
			*c_heap_table = curr_heap;
			curr_heap->bin_first[0][0] = bin_start + (i * NUM_BINS);
			curr_heap->allocated = SB_SIZE;
			curr_heap->used = 0;
			pthread_mutex_init(&curr_heap->lock, NULL);
			memset(curr_heap->bin_first[0], 0, sizeof(superblock *) * num_cpu);
		}
		memset(large_malloc_table, 0, sizeof(node) * num_cpu);

	}
	// use mem_init to initialize
	// create an array containing a heap for each thread
	// for each heap, initialize bin_first to null pointers of length NUM_BINS (superblocks will only be added to heaps using these bins)
	// (there's no longer a need to allocate a first superblock for meta_first)
	
	// consider having a bin for each block size * NUM_BINS
	
	// we should now have 9 heaps, each with just empty bins
	
	return 0;
}
