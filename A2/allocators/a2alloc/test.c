#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm_thread.h"
#include "malloc.h"
#include "memlib.h"
#include "tsc.h"

int main() {
	mm_init();
	int * a;
	int * b;
	printf("Malloc request size 3000\n");
	void * d = mm_malloc(3000);
	/*
	printf("Malloc request size 4\n");
	a = mm_malloc(4);
	printf("Malloc request size 80\n");
	b = mm_malloc(4 * 20);
	*/
	
	printf("Malloc request size 1600\n");
	int* e = mm_malloc(1600);
	printf("Malloc request size 1600\n");
	int* f = mm_malloc(1600);
	// mm_free(e);
	// mm_free(f);
	printf("Malloc request size 1600\n");
	int* g = mm_malloc(1600);
	printf("Malloc request size 1600\n");
	int* h = mm_malloc(1600);
	// int* h = mm_malloc(4);
	mm_free(g);
	mm_free(h);
	printf("Malloc request size 1600\n");
	a = mm_malloc(1600);
	printf("Malloc request size 1600\n");
	b = mm_malloc(1600);
	

	*a = 4;
	//printf("%d\n", *a);
	*e = 1;
	*f = 1;
	*g = 1;

	int i;
	for (i = 0; i < 20; i++) {
		b[i] = i;
		//printf("%d\n", b[i]);
	}

	memset(d, 1431655765, 750);
	
	/*
	for (i = 0; i < 2996; i ++) {
		printf("%d\n", (int) *(d+i));
	}
	*/
	
	printf("Free request at %p\n", a);
	mm_free(a);
	printf("Free request at %p\n", b);
	mm_free(b);
	printf("Free request at %p\n", d);
	mm_free(d);


	return 0;
}