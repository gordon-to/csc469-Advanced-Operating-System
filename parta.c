#include <stdio.h>
#include "tsc.h"
#include <stdint.h>


uint64_t inactive_periods(int num, uint64_t threshold, uint64_t *samples){

}


int main () {

	start_counter();

	uint64_t tmp = get_counter();

	printf("%u\n", tmp);	

	return 0;
}