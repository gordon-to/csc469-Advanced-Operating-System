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
	void * d = mm_malloc(3000);
	a = mm_malloc(4);
	b = mm_malloc(4 * 20);

	*a = 4;
	printf("%d\n", *a);

	int i;
	for (i = 0; i < 20; i++) {
		b[i] = i;
		printf("%d\n", b[i]);
	}

	mem_set(d, 1431655765, 750);

	for (i = 0; i < 2996; i ++) {
		printf("%d\n", (int) *(d+i));
	}

	mm_free(a);
	printf("%d\n", *a);
	mm_free(b);
	mm_free(d);


	return 0;
}