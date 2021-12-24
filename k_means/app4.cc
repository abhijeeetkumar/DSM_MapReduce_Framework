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

#include "psu_mr.h"

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
const int NUM_THREADS = 1;
const int NUM_NODES = 4;

typedef struct  threaddata{
   double partial_sum_x[NUM_CLUSTER]={0};
   double partial_sum_y[NUM_CLUSTER]={0};
   double occurence[NUM_CLUSTER]={1};
} threaddata;

int map_itr __attribute__((aligned(4096)));
int reduce_itr;
threaddata thread_data_array[NUM_THREADS*NUM_NODES];
int guard_ __attribute__((aligned(4096)));

void *kmean_map(void *threadarg)
{
  psu_mutex_lock(1);
  int id=map_itr++;
  struct threaddata *data = &thread_data_array[id];
  psu_mutex_unlock(1); 

  int index;
  double threshold;
  double dist;

  //Run only 1 iteration
  for (int iterations = 0; iterations<1; iterations++)
  {
    for (int i = id*(num_points/(NUM_THREADS*NUM_NODES)); i < (id+1)*(num_points/(NUM_THREADS*NUM_NODES)); i++)
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
  }
}

void* kmean_reduce(void* ip){
  psu_mutex_lock(1);
  int id=reduce_itr++;
  struct threaddata *data = &thread_data_array[id];
  psu_mutex_unlock(1);

        for (int k = 0; k < NUM_CLUSTER; k++)
	{
           centroid.at(k).x = data->partial_sum_x[k]/data->occurence[k];
           centroid.at(k).y = data->partial_sum_y[k]/data->occurence[k];
	}
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
   populate_points(input_file_path);
   
   //lock for changing map-reduce itr 
   map_itr = reduce_itr = 0;
   int num_nodes = psu_mr_setup(NUM_THREADS);
   if(num_nodes != NUM_NODES){
	cout<<"NUM_NODES is not matching with macro value. please update the file."<<endl;
	exit(-1);
   }
   psu_dsm_register_datasegment(&map_itr, (NUM_THREADS*NUM_NODES)*sizeof(threaddata)+ 8);
   psu_mutex_init(1);

   //Kmean- Map
   psu_mr_map(&kmean_map, NULL,NULL);

   //Kmean - Reduce
   psu_mr_reduce(&kmean_reduce, NULL, NULL);

   for (int i=0; i< num_centroid ;i++)
   {
	double x = centroid.at(i).x;
	double y = centroid.at(i).y;
	printf("New centroid. %lf %lf\n", x, y);
//	cout <<" New centroid. i: "<<fixed <<setprecision(2)<<i<<" x: "<<x<<" y: "<<y<<endl;
   }

   coordinates.clear();
   centroid.clear();
   return 0;
}
