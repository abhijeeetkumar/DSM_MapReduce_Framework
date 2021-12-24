#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>

#include "psu_mr.h"

#define SIZE_OF_DB 1020
#define NUM_THREADS 1

int NUM_NODES  = 1;

// global data of database declaration

struct keyvalue{
	char word[8];
	int value;
};
typedef struct keyvalue keyvalue;

keyvalue kv[SIZE_OF_DB] __attribute__ ((aligned (4096)));
int map_itr;
int reduce_itr;
int guard1 __attribute__((aligned(4096)));

void setup();
void *temp_hist(void *k);
void *temp_cum_hist(void* k);
void map_function(void *(*mapper)(void *), void *inpdata, void *outdata);
void reduce_function(void *(*reducer)(void *), void *inpdata, void *outdata);

int main(int argc, char *argv[])
{
	int i;
	printf("Start of region: %p\n", &kv);
	printf("map_itr at %p reduce_itr %p\n", &map_itr, &reduce_itr);
	setup();
	
	psu_mr_map(&temp_hist, NULL,NULL);

	psu_mr_reduce(&temp_cum_hist, NULL, NULL);

	sleep(50);
	return 0;
}

void setup()
{
	time_t t;
	char words[][8] = {"hello", "happy", "world", "psuni"};
	srand((unsigned) time(&t));
	int i, temp;
	for(i = 0; i < SIZE_OF_DB; ++i)
	{
		temp = rand()%4;
		strcpy(kv[i].word, words[temp]);
		kv[i].value = 0;
	}

	map_itr = reduce_itr = 0;
        NUM_NODES = psu_mr_setup(NUM_THREADS);
	psu_dsm_register_datasegment(&kv, (SIZE_OF_DB)*sizeof(keyvalue)+ 8);
	psu_mutex_init(1);
}

// histogram functions
void *temp_hist(void *k)
{
	psu_mutex_lock(1);
	int l = map_itr++;
	psu_mutex_unlock(1);

	int a = l*SIZE_OF_DB/(NUM_THREADS*NUM_NODES);
	int b = (l+1)*SIZE_OF_DB/(NUM_THREADS*NUM_NODES);
	int i;
	printf("a:%d b:%d\n",a, b);

	for(i= a; i< b; ++i)
	{
		kv[i].value = 1;
		//printf("itr pos : %d\n", i);
	}

	printf("end of map fn\n");
}

void *temp_cum_hist(void* k)
{
	int i;
	psu_mutex_lock(1);
	int a = reduce_itr++;
	psu_mutex_unlock(1);
	char words[][8] = {"hello", "happy", "world", "psuni"};
	char key[8];
	strcpy(key, words[a]); 
	int final_value = 0;

	//Going through the database
	printf("reducer is called -----%d\n",a);
	for(i = 0; i< SIZE_OF_DB; ++i)
	{
		if(!strcmp(key, kv[i].word))
			final_value++;
	}
	//Final result

	printf("Word : %s  Occurance : %d\n", key, final_value);
}

/*
// map function
void map_function(void *(*mapper)(void *), void *inpdata, void *outdata)
{
	pthread_t tid[NUM_THREADS];

	for(int j= 0; j< NUM_THREADS; ++j)
		pthread_create(&tid[j], NULL, *(mapper), (void *)j);

	for(int j= 0; j< NUM_THREADS; ++j)
		pthread_join(tid[j], NULL);

}

// reduce function
void reduce_function(void *(*reducer)(void *), void *inpdata, void *outdata)
{
	pthread_t tid[NUM_THREADS];
	//Iterative version -- 4 threads
	for(int j= 0; j< NUM_THREADS; ++j)
		pthread_create(&tid[j], NULL, *(reducer), (void *)j);

	for(int j= 0; j< NUM_THREADS; ++j)
		pthread_join(tid[j], NULL);


}
*/
