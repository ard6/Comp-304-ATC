#include <stdio.h>
#include <queue>
#include <pthread.h>
#include <iostream>
#include "pthread_sleep.c"
#include <strings.h>
#include <getopt.h>
#include <fstream>


int permittedPlaneId=-1;  // shows which plane is permitted by the tower
int planeid=0;            // a global variable for id generator. Planes take their id's here.
int t=1;                  // time constant used in pthread_sleep(2t)
int maxWaitingTime=3;     // maximum waiting time for landing planes in order to prevent starvation on departing queue
time_t start_time;        // starting time of the simulation
int isFinished=0;         // it is a boolean to check if the simulation is over


std::ofstream ATClog;     // log

pthread_mutex_t id_lock;  //    lock for the global variable permittedPlaneId
pthread_mutex_t airQueue_lock;  // lock for the landing queue
pthread_mutex_t groundQueue_lock;  // lock for the departing queue
pthread_mutex_t emergencyQueue_lock;  // lock for the emergency queue
pthread_mutex_t idGenerator_lock;     //lock for global planeid variable


pthread_mutex_t mutex;
pthread_cond_t tower_cond = PTHREAD_COND_INITIALIZER; // cond for the tower. tower will sleep while a plane using the runway

struct plane{
    int planeId;   // id of the plane
    char status;   // status of the plane. it can be 'L' , 'D' or 'E'
    time_t requestTime;  // the time when the plane is pushed to the queue
    time_t runwayTime;    // the time when the plane leaves the runway
    time_t turnaroundTime;  // runwaytime-requesttime
    pthread_mutex_t plane_mut;


};

std::queue <plane> ground;    // a queue for departing planes
std::queue <plane> air;       // a queue for landing planes
std::queue <plane> emergencyQueue;  // a queue for emergency planes

struct plane log[400];      // an array for the planes to be logged
pthread_cond_t conds[400];     // an array for plane conds


void * towerExec(void *pVoid);   //  the atc function of the towerThread
void * planeExec(void* p);       //  the function executed by the plane threads.
void * planeGenerator(void * air_probability);  //  this function is used by the generator thread and generates planes according to the probability.
void idGenerator();  // this function generates unique id's for planes.
void cmdParser(int argc, char *argv[], double &air_probability, int &simtime, int &n, int &option);  //  this method parses the command line argument
void createLog();  // it creates the log file and starts logging.
void writeLog();    // it writes the planes into the log
void writeConsole(int counter,int option);   // it creates the message to be printed on the terminal
void printQueue(std::queue<plane> queue);    // this method prints the given queue.

int main(int argc, char *argv[]){

    createLog();  //log generated

    int simtime;   // simulation time will be assigned in cmdParser
    int n;         // n is the time when the terminal prints start
    int option;    // an option for the terminal output to be dynamic or not.
    double *air_probability = static_cast<double *>(malloc(sizeof(*air_probability)));

    cmdParser(argc,argv, *air_probability,simtime,n,option);  // command line argument parsed


    if (pthread_mutex_init(&id_lock, NULL)&&pthread_mutex_init(&airQueue_lock, NULL)   // initialization of locks
    &&pthread_mutex_init(&groundQueue_lock, NULL)&&pthread_mutex_init(&emergencyQueue_lock, NULL)&&pthread_mutex_init(&idGenerator_lock, NULL)
    &&pthread_mutex_init(&mutex, NULL))   {

        return 1;
    }


    pthread_t towerThread;    // tower thread uses the towerexec method
    pthread_t generatorThread;   // thread for generating planes. it uses planeGenerator method.
    pthread_t airplaneThread;    // the initial landing plane thread
    pthread_t groundplaneThread;   // the initial departing plane  thread


    time(&start_time);  // simulation starts



    struct plane *airInitialPlane; // initial landing plane
    airInitialPlane = static_cast<plane *>(malloc(sizeof(struct plane)));
    idGenerator();  //unique id generated

    airInitialPlane->planeId = planeid;  // id is set
    airInitialPlane->status = 'L';  // status of this plane is landing
    pthread_create(&airplaneThread, NULL, planeExec, (void *) airInitialPlane);  // plane thread is created





    struct plane *groundInitialPlane;   // initial departing plane
    groundInitialPlane = static_cast<plane *>(malloc(sizeof(struct plane)));
    idGenerator();   // unique id generated

    groundInitialPlane->planeId = planeid;   // id is set
    groundInitialPlane->status = 'D';  // status of this plane is departing
    pthread_create(&groundplaneThread, NULL, planeExec, (void *) groundInitialPlane);    // plane thread is created


while(air.size()==0&&ground.size()==0){

}

    pthread_create(&towerThread,NULL,towerExec,NULL);    // tower thread created
    pthread_create(&generatorThread,NULL,planeGenerator,air_probability);   // plane generator thread created

time_t current_time;  // time_t variable to keep the current time


    while(1){
        time(&current_time);    // current time assigned



        if(difftime(current_time,start_time)>=simtime){   // if simulation time is over
            isFinished=1;                                  // then isFinished variable becomes 1
            break;                                         // and while ends.
        }

        if(difftime(current_time,start_time)>=n) {         // if it is time to print the output to the terminal
            writeConsole(difftime(current_time,start_time),option);     // then this method starts to print the output


        }

        pthread_sleep(1);           // this sleep is for making the output visible every second
    }


    writeLog();   // write the planes in the log array to the log file

    ATClog.close();     // closing the log

    pthread_join(towerThread,NULL);  //  the main thread wait for the tower thread
    pthread_join(generatorThread,NULL);  // the main thread wait for the plane generator
    std::cout<<"main finished"<<std::endl;
    return 0;
}


void * towerExec(void *pVoid){

int waitingTime;     // waiting time for the plane at the top of the queue. It is created to avoid starvation on departing plane queue
time_t waitingStartTime,cTime;    // time_t variables for when the waiting starts for a plane and current time
    time(&waitingStartTime);     // waitingStartTime is assigned

    while(1){

        if(permittedPlaneId==-1) {  // if the permitted id is -1 then the tower will permit a new plane.
            time(&cTime);         // current time is assigned
            waitingTime=difftime(cTime,waitingStartTime);     // waiting time is calculated
            if(!emergencyQueue.empty()){            // if there is an emergency plane
                pthread_mutex_lock(&id_lock);       // permitted id is locked
                permittedPlaneId = emergencyQueue.front().planeId;  // permitted plane is emergency plane
                pthread_mutex_unlock(&id_lock);    // permitted id id unlocked.

                pthread_cond_signal(&conds[emergencyQueue.front().planeId]);   // send a signal to the top of the emergency queue



                pthread_cond_wait(&tower_cond, &mutex);     // wait until the plane leaves the runway




            }else if ((!air.empty()&&ground.size()<=5)||(!air.empty()&&waitingTime>=maxWaitingTime)) {  // if there is a landing plane and no starvation on the departing plane
                                                                                                        // or there is a landing plane and starvation starts in the air

                pthread_mutex_lock(&id_lock);        // permitted id is locked
                permittedPlaneId = air.front().planeId;   // permitted plane is a landing plane.
                pthread_mutex_unlock(&id_lock);     // permitted id is unlocked

                pthread_cond_signal(&conds[air.front().planeId]);   // send a signal to the top of the landing queue

               pthread_cond_wait(&tower_cond, &mutex);    // wait until the plane leaves the runway


                time(&waitingStartTime);  // waiting start time is reset for the next landing plane

            }else if((!ground.empty()&&air.empty())||ground.size()>5){   // if there are departing planes and there is no landing planes.
                                                                            // or starvation starts in the departing queue(there are more than 5 planes)

                pthread_mutex_lock(&id_lock);     // permitted id is locked
                permittedPlaneId = ground.front().planeId;   // permitted plane is a departing plane
                pthread_mutex_unlock(&id_lock);   // permitted id is unlocked.

                pthread_cond_signal(&conds[ground.front().planeId]);    // send a signal to the top of the departing queue

                pthread_cond_wait(&tower_cond, &mutex);     // wait until the plane leaves the runway

            }else{

            }
        }
        if(isFinished){   // if the simulation time is over exit from the while
            break;
        }


    }
    std::cout<<"tower finished"<<std::endl;
    pthread_exit(NULL);    // exit from the thread
}


void * planeExec(void* p){

   if(pthread_cond_init(& conds[((struct plane*)p)->planeId],NULL)!=0){   //initialize the cond
       std::cout<< "error"<<std::endl;
   }

    pthread_mutex_init(&((struct plane*)p)->plane_mut,NULL);   //initialize the mutex

    time_t arrivalTime;  // time_t variable to keep the time when a plane is pushed to the queue
    time(&arrivalTime);  // arrival time is set

    (((struct plane*)p)->requestTime)=difftime(arrivalTime,start_time);   //  request time of the plane is set


    log[((struct plane*)p)->planeId].planeId= ((struct plane*)p)->planeId;         //****
    log[((struct plane*)p)->planeId].status= ((struct plane*)p)->status;           //****
    log[((struct plane*)p)->planeId].requestTime= ((struct plane*)p)->requestTime; //****    plane attributes are set in the log array
    log[((struct plane*)p)->planeId].runwayTime= -1;                               //****    runway time and turnaround time are initially set -1
    log[((struct plane*)p)->planeId].turnaroundTime= -1;                           //****

    if(((struct plane*)p)->status=='E'){          // if the plane is emergency plane
        pthread_mutex_lock(&emergencyQueue_lock);  // emergency queue is locked
        emergencyQueue.push(*(struct plane *) p);  // plane pushed to the emergency queue
        pthread_mutex_unlock(&emergencyQueue_lock); // emergency queue is unlocked.

    }else if(((struct plane*)p)->status=='L') {     // if the plane is landing plane
        pthread_mutex_lock(&airQueue_lock);         // landing queue is locked
        air.push(*(struct plane *) p);              // plane pushed to the landing queue
        pthread_mutex_unlock(&airQueue_lock);       // landing queue is unlocked.

    }else{                                          //if the plane is departing plane
        pthread_mutex_lock(&groundQueue_lock);      // departing queue is locked
        ground.push(*(struct plane *) p);           // plane pushed to the departing queue
        pthread_mutex_unlock(&groundQueue_lock);    // departing queue is unlocked.

    }


    pthread_cond_wait(&conds[((struct plane*)p)->planeId],&((struct plane*)p)->plane_mut);    // wait until reach the top of the queue. tower sends a signal

    pthread_sleep(2*t);   // plane uses the runway for 2t seconds.


    time_t runwayFinish;      //time_t variable to keep the time when the plane leaves the runway
    time(&runwayFinish);      // runwayFinish is set

    ((struct plane*)p)->runwayTime=difftime(runwayFinish,start_time);    // runway time of the plane is set
    ((struct plane*)p)->turnaroundTime=difftime(((struct plane*)p)->runwayTime,((struct plane*)p)->requestTime);  // turnaround time of the plane is set

    log[((struct plane*)p)->planeId].runwayTime= ((struct plane*)p)->runwayTime;     // plane attributes are set in the log array
    log[((struct plane*)p)->planeId].turnaroundTime= ((struct plane*)p)->turnaroundTime; //plane attributes are set in the log array




    if(((struct plane*)p)->status=='E'){   // if the plane is emergency plane
        pthread_mutex_lock(&emergencyQueue_lock);   // emergency queue is locked
        emergencyQueue.pop();                       // plane is popped from the queue
        pthread_mutex_unlock(&emergencyQueue_lock);  // emergency queue is unlocked

    }else if(((struct plane*)p)->status=='L') {    // if the plane is landing plane
        pthread_mutex_lock(&airQueue_lock);        // landing queue is locked
        air.pop();                                 // plane is popped from the queue
        pthread_mutex_unlock(&airQueue_lock);      // landing queue is unlocked

    }else {                                        // if the plane is departing plane
        pthread_mutex_lock(&groundQueue_lock);     // departing queue is locked
        ground.pop();                              // plane is popped from the queue
        pthread_mutex_unlock(&groundQueue_lock);   // departing queue is unlocked

    }




    pthread_mutex_lock(&id_lock);    // permitted id is locked
    permittedPlaneId=-1;             // when a plane finishes to use the runway it sets the permittedPlaneId to -1
    pthread_mutex_unlock(&id_lock);  // permitted id is unlocked


    pthread_cond_signal(&tower_cond);    // when the plane finishes to use the runway notifies the tower


    pthread_exit(NULL);        // when the plane finishes its job, exit from the thread
}

void idGenerator(){

    pthread_mutex_lock(&idGenerator_lock);   // global planeid variable is locked
    planeid++;                               // new id is generated
    pthread_mutex_unlock(&idGenerator_lock); // global planeid variable is unlocked

}

void * planeGenerator(void* air_probability){

    double ap = *((double *) air_probability);

    while(1) {

        pthread_t planeThread1;     // thread for landing planes
        pthread_t planeThread2;     // thread for departing planes

        time_t current_time;        // keeps the current time

        pthread_sleep(t);           // sleeps for t seconds to generate planes at every t seconds

        double ground_probability = 1 - ap;    // probability for the departing planes

        srand(time(0));   // feeds the random with the time
        int rnd = rand() % 100;      // takes a random number betwren 0 and 100

        int emergency;     // a variable to keep if the plane is emergency or not.

        time(&current_time);    // current time is set.

        int passedTime = difftime(current_time, start_time);   // passed time is the time between the starttime and current time
        if (passedTime > 0 && passedTime % (40 * t) == 0) {    // if passed time is greater than 0 and a multiple of 40
            emergency = 1;                                     // then it is an emergency plane
        } else {
            emergency = 0;
        }

        if (rnd < ap * 100 || emergency) {        // if random number is smaller than the air probability or it is time for the emergency

            struct plane *p1;
            p1 = static_cast<plane *>(malloc(sizeof(struct plane)));
            idGenerator();         // unique id is generated

            p1->planeId = planeid;     // id is set

            if(emergency){        // if it is emergency plane
                p1->status='E';    // then set the status with 'E'
            }else {                // else
                p1->status = 'L';    // it is a landing plane
            }

            pthread_create(&planeThread1, NULL, planeExec, (void *) p1);    // plane thread is created


        }
        if (rnd < ground_probability * 100) {     // if random number is smaller than the ground probability


            struct plane *p2;
            p2 = static_cast<plane *>(malloc(sizeof(struct plane)));
            idGenerator();     // unique id is generated

            p2->planeId = planeid;     // id is set
            p2->status='D';            // status is set to 'D'

            pthread_create(&planeThread2, NULL, planeExec, (void *) p2);      // plane thread is created


        }
        if(isFinished){        // if the simulation time is over
            break;              // exit from the while
        }
    }
    std::cout<<"generator finished"<<std::endl;
    pthread_exit(NULL);      // exit from the thread.
}

void cmdParser(int argc, char *argv[], double &air_probability, int &simtime,int &n , int &option) {
    int arg;
    simtime= 60;    // default simulation time
    air_probability = 0.5;    // default probability
    n=20;              // default n for the terminal output
    option=0;         // default option for the dynamic output.

    while ((arg = getopt(argc, argv, "s:p:n:o:")) != -1) {
        switch (arg) {
            case 's':
                 simtime= atoi(optarg);
                std::cout << " Simulation time:  "<< optarg <<std::endl;
                break;
            case 'p':
                air_probability = atof(optarg);
                std::cout << " P:  " << optarg << std::endl;
                break;
            case 'n':
                n = atof(optarg);
                std::cout << " N:  " << optarg << std::endl;
                break;
            case 'o':
                option = atof(optarg);
                break;
            default:
                exit(EXIT_FAILURE);
        }
    }
}

void createLog() {
    ATClog.open("./atc.log");
    ATClog << "planeID \t Status \t Request-Time \t Runway-Time \t Turnaround-Time\n";
    ATClog << "_________________________________________________________________________________________________________\n";
}

void writeLog(){

    for(int i=1;i<planeid;i++){   // number of planes
        char runwayTime[10]=" ";   // runwayTime string is a space
        char turnaroundTime[10]=" ";  // turnaroundTime string is a space

        if(log[i].runwayTime!=-1){   // if the plane's runwaytime is set to a number
            sprintf(runwayTime,"%ld",log[i].runwayTime);
        }
        if(log[i].turnaroundTime!=-1){     // if the plane's turnaround time is set to a number
            sprintf(turnaroundTime,"%ld",log[i].turnaroundTime);
        }
        ATClog << log[i].planeId<<" \t\t "<<log[i].status<<" \t\t "<< log[i].requestTime<<" \t\t "<<runwayTime <<"\t\t " <<turnaroundTime<<"\n";
    }
}

void writeConsole(int counter ,int option){

    if(option==1){  // if option is 1
        system("clear");   // then every time clear the terminal
    }

    std::cout<< "\n\n \t\t second " << counter<<""<<std::endl;   // prints the second
    std::cout<<"____________________________________________\n";
    std::cout<< "landing planes: ";
    printQueue(air);                                                 // prints the landing queue
    std::cout<< "emergency planes: ";
    printQueue(emergencyQueue);                                        // prints the emergency queue
    std::cout<<"____________________________________________\n";
    std::cout<<"== == == == == == == == == == == == == == == \n";
    std::cout<<"_____________________________________________ \n";
    std::cout<< "departing planes: ";
    printQueue(ground);                                                 // prints the departing queue


}
void printQueue(std::queue<plane> queue){
    int id;            // integer variable for temp id
    while (!queue.empty()){    // while the queue is not empty
        id=queue.front().planeId;    // set the temp id with the id of the top plane in the queue
        std::cout<<" "<<id;
        queue.pop();                //pop the top element from the queue
    }
    std::cout<<"\n";
}