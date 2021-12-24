#include "psu_lock.h"
#include "psu_dsm_system.h"

static int _gnthreads = 1;
int number_machines __attribute__ ((aligned (4096)));
int guard_band __attribute__ ((aligned (4096)));

int psu_mr_setup(unsigned int nthreads){
	_gnthreads = nthreads;

	std::ifstream myfile("node_list.txt");

        // count the newlines with an algorithm specialized for counting:
	string line;
	for (number_machines = 0; std::getline(myfile, line); ++number_machines);
	cout<<number_machines<<endl;

	int return_value = number_machines;
	psu_dsm_register_datasegment(&number_machines, 4096);
	psu_mutex_init(0);

	return return_value;	
}

void barrier(){
        psu_mutex_lock(0);
        number_machines--;
        psu_mutex_unlock(0);

        while(number_machines > 0);
}

void psu_mr_map(void* (*map_fp)(void*), void *inpdata, void *opdata){
	pthread_t tid[_gnthreads];

	for(int j= 0; j< _gnthreads; ++j)
		pthread_create(&tid[j], NULL, *(map_fp), inpdata);

	for(int j= 0; j< _gnthreads; ++j)
		pthread_join(tid[j], &opdata);

	barrier();
}

void psu_mr_reduce(void* (*reduce_fp)(void*), void *ipdata, void *opdata){
	pthread_t tid[_gnthreads];

	//Iterative version -- _gnthreads
	for(int j= 0; j< _gnthreads; ++j)
		pthread_create(&tid[j], NULL, *(reduce_fp), ipdata);

	for(int j= 0; j< _gnthreads; ++j)
		pthread_join(tid[j], &opdata);

	barrier();
}
