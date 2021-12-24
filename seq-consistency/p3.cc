//Program 3 of the sequential consistency check

#include <stdlib.h>
#include <stdio.h>
#include "psu_dsm_system.h"
#include <malloc.h>

//int a __attribute__ ((aligned (4096)));
//int b __attribute__ ((aligned (4096)));
//int b_ __attribute__ ((aligned (4096)));


int main(int argc, char* argv[])
{
        int *a = (int *)memalign(4096,4096);
        int *b = (int *)memalign(4096,4096);
        printf("%p %p", a, b);
	memset((char*)a, 0, 4096);
        memset((char*)b, 0, 4096);

        psu_dsm_malloc((char*)a, 4096);
        psu_dsm_malloc((char *)b, 4096);    
	//psu_dsm_register_datasegment(&a, 4096*2);
	while (*b != 1);

	printf("a = %d and b = %d\n",*a,*b);
	return 0;
}
