#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cstring>
#include <pthread.h>
#include<queue>

using namespace std;

//Kmean related variable
#define NUM_CLUSTER 4
string input_file_path = " ";
int num_points = 0;
int num_centroid = 0;
struct Point {
    double x;
    double y;
    
    Point()
    {
	x = 0;
        y = 0;
    }	
};
deque<Point> coordinates;
deque<Point> centroid;

//pthread related variable
int NUM_THREADS;
pthread_mutex_t lock;
pthread_cond_t cv;
int barrier_val;
struct  threaddata{
   int	thread_id;
   double partial_sum_x[NUM_CLUSTER]={0};
   double partial_sum_y[NUM_CLUSTER]={0};
   double occurence[NUM_CLUSTER]={1};
};

void *kmean(void *threadarg)
{
  struct threaddata *data;
  data=(struct threaddata *) threadarg;
  int id=data->thread_id;
  int index;
  double threshold;
  double dist;
  //Run only 1 iteration
  for (int iterations = 0; iterations<1; iterations++)
  {
    for (int i = id*(num_points/NUM_THREADS); i < (id+1)*(num_points/NUM_THREADS); i++)
    {
     //cout<<" thread id "<<id<<" i: "<<i<<endl;
     threshold = DBL_MAX;
     index=-1;
     for (int j = 0; j < NUM_CLUSTER; j++)
     {
	double x_val = pow(coordinates.at(i).x - centroid.at(j).x,2);
	double y_val = pow(coordinates.at(i).y - centroid.at(j).y,2);
	dist = sqrt(x_val + y_val);     
	if (dist < threshold)
	{
          threshold = dist;
	  index = j;
	}
     }	
     data->partial_sum_x[index]+=coordinates.at(i).x;
     data->partial_sum_y[index]+=coordinates.at(i).y;
     data->occurence[index]+=1;
    }

    pthread_mutex_lock(&lock);
    if (barrier_val<NUM_THREADS-1)
     {
	barrier_val++;
	pthread_cond_wait(&cv,&lock);
     }

    if(barrier_val==NUM_THREADS-1){
        for (int k = 0; k < NUM_CLUSTER; k++)
	{
           centroid.at(k).x = data->partial_sum_x[k]/data->occurence[k];
           centroid.at(k).y = data->partial_sum_y[k]/data->occurence[k];
	}
	barrier_val=0;
	pthread_cond_broadcast(&cv);
     }
     pthread_mutex_unlock(&lock);
   }
   pthread_exit(NULL);
}

void populate_points(string input_file_path)
{
  ifstream input_file;
  input_file.open(input_file_path, ios::in);
  if(!input_file) 
  { 
	cout<<"No such file"<<endl;
	exit(-1);
  } else {
    int lineno = 0;
    string line;
    while ( getline (input_file,line) )
    {
      //cout << line << endl;
      istringstream line_stream(line);
      if(lineno == 0) {
	    line_stream >> num_points;
      } else if (lineno == 1) {
	    line_stream >> num_centroid;
      } else {  
	    Point p;
	    line_stream >> p.x >> p.y;
	    //cout <<" x: " <<p.x <<" y : "<< p.y << endl;
	    if (lineno >= 2+num_points) {
	      centroid.push_back(p);
	    } else {
	      coordinates.push_back(p);
	    }
      }
      lineno++;
    }

    //Sanity check
    if(num_centroid != centroid.size()) {
       cout<<"Error!! Incorrect size"<<endl;
       exit(-1);
    }

    if(num_points != coordinates.size()) {
       cout<<"Error!! Incorrect size"<<endl;
       exit(-1);
    }
  }
}  


int main ( int argc, char* argv[])
{ 
   input_file_path = argv[1];
   NUM_THREADS = atoi(argv[2]);
   populate_points(input_file_path);
   
   //Thread init
   pthread_mutex_init(&lock,NULL);
   pthread_cond_init(&cv,NULL);

   //Kmean
   int thread_error_code;
   void *status;
   pthread_t threads[NUM_THREADS];
   struct threaddata thread_data_array[NUM_THREADS];

   //Fork kmean() num_threads time
   for( int i = 0; i< NUM_THREADS; i++ ) {
      thread_data_array[i].thread_id = i;
      thread_error_code = pthread_create(&threads[i], NULL, kmean, (void *) &thread_data_array[i]);
      if (thread_error_code) {
	 cout << "Error:unable to create thread," << thread_error_code << endl;
	 exit(-1);
      }
   }
   
   //Join all threads
   for (int j = 0; j < NUM_THREADS; j++) {
       thread_error_code=pthread_join (threads [j], &status);
       if (thread_error_code) {
           printf("ERROR; return code from pthread_join() is %d\n", thread_error_code);
           exit(-1);
       }
   }

   for (int i=0; i< num_centroid ;i++)
   {
	double x = centroid.at(i).x;
	double y = centroid.at(i).y;
	cout <<" New centroid. i: "<<fixed <<setprecision(2)<<i<<" x: "<<x<<" y: "<<y<<endl;
   }

   //Thread destory
   pthread_mutex_destroy(&lock);
   pthread_cond_destroy(&cv);
   
   coordinates.clear();
   centroid.clear();
   return 0;
}
