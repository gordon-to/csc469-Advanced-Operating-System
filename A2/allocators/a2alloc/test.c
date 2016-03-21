#include <stdio.h>
#include <stdlib.h>
#include "mm_thread.h"
#include "malloc.h"
#include "memlib.h"
#include "tsc.h"

int main() {
	mm_init();
	int * a;
	int * b;
	a = mm_malloc(4);
	b = mm_malloc(4 * 20);
	*a = 4;
	printf("%d\n", *a);

	int i;
	for (i = 0; i < 20; i++) {
		b[i] = i;
		printf("%d\n", b[i]);
	}

	mm_free(a);
	printf("%d\n", *a);
	mm_free(b);
	

	return 0;
}