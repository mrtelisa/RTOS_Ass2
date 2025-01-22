//compile with: g++ -lpthread <sourcename> -o <executablename>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <sched.h>

//code of periodic tasks
void task1_code( );
void task2_code( );
void task3_code( );

//code of aperiodic tasks (if any)
void task4_code( );

//characteristic function of the thread, only for timing and synchronization
//periodic tasks
void *task1( void *);
void *task2( void *);
void *task3( void *);

//aperiodic tasks
void *task4( void* );

// initialization of mutexes and conditions (only for aperiodic scheduling)
pthread_mutex_t mutex_task_4 = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_task_4 = PTHREAD_COND_INITIALIZER;

// ---------------------------------------------------------------------------- 

#define INNERLOOP 1000
#define OUTERLOOP 2000

#define NPERIODICTASKS 3
#define NAPERIODICTASKS 1
#define NTASKS NPERIODICTASKS + NAPERIODICTASKS

long int periods[NTASKS];
struct timespec next_arrival_time[NTASKS];
double WCET[NTASKS];
pthread_attr_t attributes[NTASKS];
pthread_t thread_id[NTASKS];
struct sched_param parameters[NTASKS];
int missed_deadlines[NTASKS];
void k_write(const char *const str){
    int fd, result, len;
    if((fd = open("/dev/simple", O_WRONLY)) < 0){
        perror("Open failed");
        return;
    }
    len = strlen(str)+1;
    if((result = write(fd, str, len)) != len){
        perror("Write failed");
        return;
    }
    close(fd);
}

int main(){

    // initializing the periods of each task
    periods[0] = 300000000; //in nanoseconds
    periods[1] = 500000000; //in nanoseconds
    periods[2] = 800000000; //in nanoseconds

    //for aperiodic tasks we set the period equals to 0
    periods[3] = 0;

    //this is not strictly necessary, but it is convenient to assign a name to the maximum and the 
    //minimum priotity in the system. We call them priomin and priomax.

    struct sched_param priomax;
    priomax.sched_priority=sched_get_priority_max(SCHED_FIFO);
    struct sched_param priomin;
    priomin.sched_priority=sched_get_priority_min(SCHED_FIFO);

    // set the maximum priority to the current thread (you are required to be
    // superuser). Check that the main thread is executed with superuser privileges
    // before doing anything else.

    if(getuid() == 0)
        pthread_setschedparam(pthread_self(),SCHED_FIFO,&priomax);

    // execute all tasks in standalone modality in order to measure execution times
    // (use gettimeofday). Use the computed values to update the worst case execution
    // time of each task.

    for(int i = 0; i < NTASKS; i++){

        // initializa time_1 and time_2 required to read the clock
        struct timespec time_1, time_2;
        clock_gettime(CLOCK_REALTIME, &time_1);

        //we should execute each task more than one for computing the WCET
        //periodic tasks
        if (i==0)
            task1_code();
        if (i==1)
            task2_code();
        if (i==2)
            task3_code();
            
        //aperiodic tasks
        if (i==3)
            task4_code();

        clock_gettime(CLOCK_REALTIME, &time_2);

        // compute the Worst Case Execution Time (in a real case, we should repeat this many times under
        //different conditions, in order to have reliable values

        WCET[i]= 1000000000*(time_2.tv_sec - time_1.tv_sec) + (time_2.tv_nsec-time_1.tv_nsec);
        printf("\nWorst Case Execution Time %d=%f \n", i, WCET[i]);
    }

    // ---------------------------------------------------------------------------- schedulability

    // compute U
    double U = WCET[0]/periods[0]+WCET[1]/periods[1]+WCET[2]/periods[2];

    // compute Ulub by considering the fact that we have harmonic relationships between periods
    double Ulub = 1;
        
    //if there are no harmonic relationships, use the following formula instead
    //double Ulub = NPERIODICTASKS*(pow(2.0,(1.0/NPERIODICTASKS)) -1);
    
    //check the sufficient conditions: if they are not satisfied, exit  
    if(U > Ulub){
        printf("\n U=%lf Ulub=%lf Non schedulable Task Set", U, Ulub);
        return(-1);
    }

    printf("\n U=%lf Ulub=%lf Scheduable Task Set", U, Ulub);
    fflush(stdout);
    sleep(5);
    
    // ----------------------------------------------------------------------------

    // set the minimum priority to the current thread: this is now required because 
    //we will assign higher priorities to periodic threads to be soon created pthread_setschedparam

    if(getuid() == 0){
        pthread_setschedparam(pthread_self(),SCHED_FIFO,&priomin);
    }
  
    // set the attributes of each task, including scheduling policy and priority
    for(int i =0; i < NPERIODICTASKS; i++){
        //initialize the attribute structure of task i
        pthread_attr_init(&(attributes[i]));

        //set the attributes to tell the kernel that the priorities and policies are explicitly chosen,
        //not inherited from the main thread (pthread_attr_setinheritsched) 
        pthread_attr_setinheritsched(&(attributes[i]), PTHREAD_EXPLICIT_SCHED);
      
        // set the attributes to set the SCHED_FIFO policy (pthread_attr_setschedpolicy)
        pthread_attr_setschedpolicy(&(attributes[i]), SCHED_FIFO);

        //properly set the parameters to assign the priority inversely proportional to the period
        parameters[i].sched_priority = priomin.sched_priority+NTASKS - i;

        //set the attributes and the parameters of the current thread (pthread_attr_setschedparam)
        pthread_attr_setschedparam(&(attributes[i]), &(parameters[i]));
    }

    // aperiodic tasks
    for(int i = NPERIODICTASKS; i < NTASKS; i++){
        pthread_attr_init(&(attributes[i]));
        pthread_attr_setschedpolicy(&(attributes[i]), SCHED_FIFO);

        //set minimum priority (background scheduling)
        parameters[i].sched_priority = 0;
        pthread_attr_setschedparam(&(attributes[i]), &(parameters[i]));
    }

    //declare the variable to contain the return values of pthread_create   
    int iret[NTASKS];

    //declare variables to read the current time
    struct timespec time_1;
    clock_gettime(CLOCK_REALTIME, &time_1);

    // set the next arrival time for each task. This is not the beginning of the first
    // period, but the end of the first period and beginning of the next one. 
    for(int i = 0; i < NPERIODICTASKS; i++){
        long int next_arrival_nanoseconds = time_1.tv_nsec + periods[i];
        //then we compute the end of the first period and beginning of the next one
        next_arrival_time[i].tv_nsec= next_arrival_nanoseconds%1000000000;
        next_arrival_time[i].tv_sec= time_1.tv_sec + next_arrival_nanoseconds/1000000000;
        missed_deadlines[i] = 0;
    }
    
    // ---------------------------------------------------------------------------- threads management

    // create all threads
    iret[0] = pthread_create( &(thread_id[0]), &(attributes[0]), task1, NULL);
    iret[1] = pthread_create( &(thread_id[1]), &(attributes[1]), task2, NULL);
    iret[2] = pthread_create( &(thread_id[2]), &(attributes[2]), task3, NULL);
    iret[3] = pthread_create( &(thread_id[3]), &(attributes[3]), task4, NULL);

    // join all threads
    pthread_join( thread_id[0], NULL);
    pthread_join( thread_id[1], NULL);
    pthread_join( thread_id[2], NULL);
    pthread_join( thread_id[3], NULL);
    
    // set the next arrival time for each task. This is not the beginning of the first
    // period, but the end of the first period and beginning of the next one. 
    for(int i = 0; i < NTASKS; i++){
        printf ("\nMissed Deadlines Task %d=%d", i, missed_deadlines[i]);
        fflush(stdout);
    }
    exit(0);
}

// ---------------------------------------------------------------------------- defining what is the specific application of each task

void task1_code(){  
    // write to device file
    k_write("1["); fflush(stdout);

    //this double loop with random computation is only required to waste time
    double uno;

    for(int i = 0; i < OUTERLOOP; i++){
        for(int j = 0; j < INNERLOOP; j++){
            uno = rand()*rand()%10;
        }
    }
    
    // write to device file
    k_write("]1 "); fflush(stdout);
}

//thread code for task_0 (used only for temporization)
void *task1( void *ptr){
    // set thread affinity, that is the processor on which threads shall run
    cpu_set_t cset;
    CPU_ZERO (&cset);
    CPU_SET(0, &cset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cset);

    //execute the task one hundred times... it should be an infinite loop (too dangerous)
    for(int i=0; i < 100; i++){
        // execute application specific code
        task1_code();

        // sleep until the end of the current period (which is also the start of the new one)
        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &next_arrival_time[0], NULL);

        // the thread is ready and can compute the end of the current period for the next iteration
        long int next_arrival_nanoseconds = next_arrival_time[0].tv_nsec + periods[0];
        next_arrival_time[0].tv_nsec= next_arrival_nanoseconds%1000000000;
        next_arrival_time[0].tv_sec= next_arrival_time[0].tv_sec + next_arrival_nanoseconds/1000000000;
    }
    return NULL;
}


void task2_code(){
    // write to device file
    k_write("2["); fflush(stdout);

    //this double loop with random computation is only required to waste time
    double uno;

    for(int i = 0; i < OUTERLOOP; i++){
        for(int j = 0; j < INNERLOOP; j++){
            uno = rand()*rand()%10;
        }
    }
        
    // ---------------------------------------------------------------------------- call of the aperiodi task
    if(uno == 0){
        //pthread_mutex_lock(&mutex_task_4); 
        k_write(":ex(4) "); fflush(stdout);
        pthread_cond_signal(&cond_task_4);
        //pthread_mutex_unlock(&mutex_task_4); 
    }
    // ----------------------------------------------------------------------------

    // write to device file
    k_write("]2 "); fflush(stdout);
}

void *task2( void *ptr ){
    // set thread affinity, that is the processor on which threads shall run
    cpu_set_t cset;
    CPU_ZERO (&cset);
    CPU_SET(0, &cset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cset);

    for(int i=0; i < 100; i++){
        task2_code();

        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &next_arrival_time[1], NULL);
        long int next_arrival_nanoseconds = next_arrival_time[1].tv_nsec + periods[1];
        next_arrival_time[1].tv_nsec= next_arrival_nanoseconds%1000000000;
        next_arrival_time[1].tv_sec= next_arrival_time[1].tv_sec + next_arrival_nanoseconds/1000000000;
    }
    return NULL;
}


void task3_code(){
    // write to device file
    k_write("3["); fflush(stdout);

    //this double loop with random computation is only required to waste time
    double uno;

    for(int i = 0; i < OUTERLOOP; i++){
        for(int j = 0; j < INNERLOOP; j++){
            uno = rand()*rand()%10;
        }
    }
    
    k_write("]3 "); fflush(stdout);
}

void *task3( void *ptr){
    // set thread affinity, that is the processor on which threads shall run
    cpu_set_t cset;
    CPU_ZERO (&cset);
    CPU_SET(0, &cset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cset);

    for(int i=0; i < 100; i++){
        task3_code();

        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &next_arrival_time[2], NULL);
        long int next_arrival_nanoseconds = next_arrival_time[2].tv_nsec + periods[2];
        next_arrival_time[2].tv_nsec= next_arrival_nanoseconds%1000000000;
        next_arrival_time[2].tv_sec= next_arrival_time[2].tv_sec + next_arrival_nanoseconds/1000000000;
    }
    return NULL;
}

// ---------------------------------------------------------------------------- aperiodic task

void task4_code(){
    k_write("4["); fflush(stdout);

    for(int i = 0; i < OUTERLOOP; i++){
        for(int j = 0; j < INNERLOOP; j++){
            rand() * rand();
        }
    }
    k_write("4["); fflush(stdout);
}

void *task4(void *){    
    cpu_set_t cset;
    CPU_ZERO(&cset);
    CPU_SET(0, &cset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cset);

    while(1){    
        //pthread_mutex_lock(&mutex_task_4);  
        pthread_cond_wait(&cond_task_4, &mutex_task_4);
        //pthread_mutex_unlock(&mutex_task_4); 

        task4_code();
    }
}
