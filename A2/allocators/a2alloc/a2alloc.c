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
static const size_t block_sizes[9] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048};
static size_t pg_size;

// Fullness binning
#define FULLNESS_DENOMINATOR 4				// Amount to divide S for each fullness bin, and will also act as empty fraction (f)
#define NUM_BINS FULLNESS_DENOMINATOR + 2	// Empty, 1-24%, 25-49%, 50-74%, 75-99%, Full
#define K 8									// Minimum superblock threshold (K)

// Typedefs for all the necessary memory objects
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
	// Node large malloc linked lists
	int type;					// Indicator that this is a large block (set to 1)
	int num_pages;
	int heap_id;				// If this belongs to the global heap, indicates that the chunk is free
	void * prev;
	void * next;
} node;

typedef struct {
	pthread_mutex_t lock;
	int allocated;				// Space allocated to the heap in bytes
	int used;					// Space used within the heap in bytes
	node* bin_large;			// Bin for large mallocs
	// 2D Array of pointers to first superblock in each bin
	// The first range specifies block size class (the ones from 8 - 2048)
	// The second range specifies fullness (from 0 - 100%)
	superblock* bin_first[9][NUM_BINS];
} heap;

// pointer to hold all heaps
heap ** heap_table;

// global locks
pthread_mutex_t sbrk_lock = PTHREAD_MUTEX_INITIALIZER;

/*******************************
	FUNCTIONS START
*******************************/

/* Creates a superblock at the given address and returns the pointer to it. */
superblock *new_superblock(void* ptr, size_t block_size, int heap_id) {
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
			new_sb->block_map[0] = (char)pow(2, blocks_used) - 1;
		} else {
			new_sb->block_map[0] = 255;
			new_sb->block_map[1] = (char)pow(2, blocks_used % 8) - 1;
		}
		heap_table[heap_id]->used += blocks_used * new_sb->block_size;
	}
	
	new_sb->heap_id = heap_id;
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
		// printf("From heap %d: bin[%d][%d]\n", orig[0], orig[1], orig[2]);
		// printf("To heap %d: bin[%d][%d]\n", dest[0], dest[1], dest[2]);
		
		int bytes_moved = sb->used_blocks * sb->block_size;
		heap_table[orig[0]]->used -= bytes_moved;
		heap_table[orig[0]]->allocated -= SB_SIZE;
		heap_table[dest[0]]->used += bytes_moved;
		heap_table[dest[0]]->allocated += SB_SIZE;
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
		if(block_map[j] < 255) {
			for(k = 0; k < 8; k++) {
				if(!((block_map[j] >> k) % 2)) {
					// Block at bit k is free
					return j * 8 + k;
				}
			}
		}
	}
	
	// If we somehow fell out of all of that, means that block_map is full
	return -1;
}

/* Gets the CPU ID of the current calling thread */
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

/* Malloc for large request sizes (>S/2) */
void *malloc_large(size_t sz, int cpu_id) {
	int num_pages = ceil((float)(sz + sizeof(node))/(float)SB_SIZE);
	node* target = NULL;
	
	// Search the global heap for any free space
	pthread_mutex_lock(&heap_table[0]->lock);
	node* curr = heap_table[0]->bin_large;
	while(curr) {
		if(curr->num_pages >= num_pages) {
			// We found a space that will fit our request
			target = curr;
			node* new_next = NULL;
			node* new_prev = NULL;
			
			// Take only what we need from the chunk			
			if(curr->num_pages > num_pages) {
				// Need to adjust the empty space in the global heap
				node* adjusted_head = (node*)((unsigned long)curr + (num_pages * pg_size));
				
				adjusted_head->type = 1;
				adjusted_head->num_pages = curr->num_pages - num_pages;
				adjusted_head->heap_id = 0;
				adjusted_head->prev = curr->prev;
				adjusted_head->next = curr->next;
				
				new_next = adjusted_head;
				new_prev = adjusted_head;
			} else {
				new_next = curr->next;
				new_prev = curr->prev;
			}
			
			if(curr->prev) {
				((node*)curr->prev)->next = new_next;
			} else {
				heap_table[0]->bin_large = new_next;
			}
			if(curr->next) ((node*)curr->next)->prev = new_prev;
			
			break;
		}
		
		curr = curr->next;
	}
	pthread_mutex_unlock(&heap_table[0]->lock);
	
	// If there is no space, use sbrk
	if(!target) {
		pthread_mutex_lock(&sbrk_lock);
		target = (node*)mem_sbrk(num_pages * pg_size);
		pthread_mutex_unlock(&sbrk_lock);
	}
	
	// Now insert the new block into the heap's large malloc bin and initialize
	pthread_mutex_lock(&heap_table[cpu_id+1]->lock);
	if(heap_table[cpu_id+1]->bin_large) {
		heap_table[cpu_id+1]->bin_large->prev = target;
	}
	target->next = heap_table[cpu_id+1]->bin_large;
	target->prev = NULL;
	heap_table[cpu_id+1]->bin_large = target;
	
	target->type = 1;
	target->num_pages = num_pages;
	target->heap_id = cpu_id;
	pthread_mutex_unlock(&heap_table[cpu_id+1]->lock);
	
	return (target + 1);
}

/* Free for large allocated blocks (type = 1) */
void free_large(node* target) {
	// For the sake of efficiency, we're going to use the page border pointer
	// that was already calculated in mm_free() as the input
	int HID = target->heap_id;
	
	// Remove the target block from its original heap
	pthread_mutex_lock(&heap_table[HID]->lock);
	if(target->prev) {
		((node*)target->prev)->next = target->next;
	} else {
		heap_table[HID]->bin_large = target->next;
	}
	if(target->next) ((node*)target->next)->prev = target->prev;
	pthread_mutex_unlock(&heap_table[HID]->lock);
	
	// Insert the block to the beginning of the global (free) heap
	pthread_mutex_lock(&heap_table[0]->lock);
	if(heap_table[0]->bin_large) {
		heap_table[0]->bin_large->prev = target;
	}
	target->next = heap_table[0]->bin_large;
	target->prev = NULL;
	heap_table[0]->bin_large = target;
	pthread_mutex_unlock(&heap_table[0]->lock);
	
	return;
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
	pthread_mutex_lock(&heap_table[cpu_id+1]->lock);
	
	// Find a fairly full superblock with the appropriate block_size
	// Go from nearly full bin to empty bin
	for(i = NUM_BINS - 2; i >= 0; i--) {
		superblock* sb = heap_table[cpu_id+1]->bin_first[size_id][i];
		
		// Searching bin for superblock with space
		while(sb) {
			// Special case check for first block (shared with metadata)
			int cant_use_first = 0;
			if(!(sb->block_map[0] % 2)) {
				// If the first block is "free" but the request size doesn't
				// fit in that space, then we have to consider the block as 
				// unusable for this case.
				if(sz > size_class - sizeof(superblock)) {
					cant_use_first = 1;
				} else {
					use_first = 1;
					target_sb = sb;
					old_fullness = i;
					break;
				}
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
				// printf("Take empty bin from global heap:\n");
				// printf("bin %d: address %p\n", i, heap_table[0]->bin_first[i][0]);
				target_sb = heap_table[0]->bin_first[i][0];
				origin[0] = 0;
				origin[1] = i;
				origin[2] = 0;
				
				// Reset the superblock (but keep it in the same bin)
				superblock* next = target_sb->next;
				heap_table[0]->used -= target_sb->used_blocks * target_sb->block_size;
				target_sb = new_superblock(target_sb, size_class, 0);
				target_sb->next = next;
				break;
			}
		}
		
		// check heap 0's bin 1 for size_id (25% bin)
		if(!target_sb && heap_table[0]->bin_first[size_id][1]) {
			// printf("Take almost empty bin from global heap.\n");
			target_sb = heap_table[0]->bin_first[size_id][1];
			origin[0] = 0;
			origin[1] = size_id;
			origin[2] = 1;
		}
		
		// If there literally were no blocks, we'll have to sbrk for one
		if(!target_sb) {
			pthread_mutex_unlock(&heap_table[0]->lock);
			// printf("sbrk for new space.\n");
			pthread_mutex_lock(&sbrk_lock);
			void* tmp = mem_sbrk(pg_size);
			pthread_mutex_unlock(&sbrk_lock);
			if(!tmp) {
				// No more space!
				printf("Error: No more space in heap.");
				return NULL;
			}
			
			// Initialize the superblock
			target_sb = new_superblock(tmp, size_class, cpu_id + 1);
			// Slot the superblock into the heap
			heap_table[cpu_id+1]->bin_first[size_id][0] = target_sb;
			heap_table[cpu_id+1]->allocated += SB_SIZE;
			old_fullness = 0;
		} else {
			// Move the superblock to the appropriate heap's bin_first
			int destination[3] = {cpu_id+1, size_id, 1};
			transfer_bins(target_sb, origin, destination);
			old_fullness = 1;
			pthread_mutex_unlock(&heap_table[0]->lock);
		}
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
		if(block_id == -1) {
			// For some reason there was no space
			printf("Error: Superblock has no free space.");
			return NULL;
		}
		block = (char*)target_sb + block_id * size_class;
		// printf("target_sb + (block_id * size_class) = block\n");
		// printf("%p + %d = %p\n", target_sb, block_id * size_class, block);
		
		// Change block map and used_blocks
		target_sb->block_map[block_id/8] += pow(2, block_id % 8);
	}
	target_sb->used_blocks++;
	heap_table[cpu_id+1]->used += size_class;
	
	// If the bin has changed fullness, we'll need to move it accordingly
	float new_percent = (float)target_sb->used_blocks / (float)(SB_SIZE / size_class);
	int new_fullness = (int)(new_percent * FULLNESS_DENOMINATOR) + 1;
	if(new_fullness != old_fullness) {
		int origin[3] = {cpu_id+1, size_id, old_fullness};
		int destination[3] = {cpu_id+1, size_id, new_fullness};
		transfer_bins(target_sb, origin, destination);
	}
	
	/*
	printf("Malloc: Allocated address %p; size = %d\n", block, (int)size_class);
	printf("block_size: %d\n", (int)target_sb->block_size);
	printf("block_map[0]: %hhu\n", target_sb->block_map[0]);
	printf("block_map[1]: %hhu\n", target_sb->block_map[1]);
	printf("block_map[2]: %hhu\n", target_sb->block_map[2]);
	printf("used_blocks: %d\n", target_sb->used_blocks);
	printf("heap_id: %d\n", target_sb->heap_id);
	printf("prev: %p\n", target_sb->prev);
	printf("next: %p\n\n", target_sb->next);
	*/
	
	pthread_mutex_unlock(&heap_table[cpu_id+1]->lock);
	
	return block;
}

void mm_free(void *ptr) {
	int SID, HID;			// size_id and heap_id
	
	// Move up to page border to read header data
	void* page = (int*)((unsigned long)ptr - ((unsigned long)ptr % mem_pagesize()));
	unsigned long offset = (unsigned long)ptr % mem_pagesize();
	int type = *(int*)page;
	
	// Freeing for large blocks
	if(type) {
		free_large((node*)page);
		return;
	}
	
	// Get the superblock data and deallocate it from the superblock
	superblock* sb = (superblock*)page;
	HID = sb->heap_id;
	pthread_mutex_lock(&heap_table[HID]->lock);
	
	// Updating block map
	int block_id = offset / sb->block_size;
	// printf("Free: block_id = %d (status: %d)\n\n", block_id, ((unsigned char)sb->block_map[block_id/8] >> (block_id % 8)) % 2);
	sb->block_map[block_id/8] -= pow(2, block_id % 8);
	
	// Book-keeping variables
	float old_percent = (float)sb->used_blocks / (float)(SB_SIZE / sb->block_size);
	int old_fullness = (int)(old_percent * FULLNESS_DENOMINATOR) + 1;
	for(SID = 0; SID < 9; SID++) {
		if(sb->block_size <= block_sizes[SID]) break;
	}
	sb->used_blocks--;
	heap_table[HID]->used -= sb->block_size;
	
	// Update fullness bins
	float new_percent = (float)sb->used_blocks / (float)(SB_SIZE / sb->block_size);
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
	// printf("Heap %d usage: %d / %d\n", HID, heap_table[HID]->used, heap_table[HID]->allocated);
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
	}
	
	pthread_mutex_unlock(&heap_table[HID]->lock);
}

int mm_init(void) {
	if (!mem_init()) {
		int num_cpu = getNumProcessors();
		pg_size = mem_pagesize();
		heap_table = (heap **) mem_sbrk(pg_size);

		int i;
		for (i = 0; i <= num_cpu; i++) {
			// Allocate a page for each heap
			heap_table[i] = (heap *)mem_sbrk(pg_size);
			
			// Initialize the heaps
			pthread_mutex_init(&heap_table[i]->lock, NULL);
			heap_table[i]->allocated = 0;
			heap_table[i]->used = 0;
			heap_table[i]->bin_large = NULL;
			memset(heap_table[i]->bin_first, 0, sizeof(superblock*) * 9 * NUM_BINS);
		}
	}
	return 0;
}
