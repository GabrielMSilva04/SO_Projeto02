#define CHECKING_IN 0
#define AT_TABLE 1
#define WAITING 2
// other states as needed...

/**
 *  \file semSharedMemGroup.c (implementation file)
 *
 *  \brief Problem name: Restaurant
 *
 *  Synchronization based on semaphores and shared memory.
 *  Implementation with SVIPC.
 *
 *  Definition of the operations carried out by the groups:
 *     \li goToRestaurant
 *     \li checkInAtReception
 *     \li orderFood
 *     \li waitFood
 *     \li eat
 *     \li checkOutAtReception
 *
 *  \author Nuno Lau - December 2023
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <math.h>

#include "probConst.h"
#include "probDataStruct.h"
#include "logging.h"
#include "sharedDataSync.h"
#include "semaphore.h"
#include "sharedMemory.h"

/** \brief logging file name */
static char nFic[51];

/** \brief shared memory block access identifier */
static int shmid;

/** \brief semaphore set access identifier */
static int semgid;

/** \brief pointer to shared memory region */
static SHARED_DATA *sh;

static void goToRestaurant (int id);
static void checkInAtReception (int id);
static void orderFood (int id);
static void waitFood (int id);
static void eat (int id);
static void checkOutAtReception (int id);


/**
 *  \brief Main program.
 *
 *  Its role is to generate the life cycle of one of intervening entities in the problem: the group.
 */
int main (int argc, char *argv[])
{
    int key;                                         /*access key to shared memory and semaphore set */
    char *tinp;                                                    /* numerical parameters test flag */
    int n;

    /* validation of command line parameters */
    if (argc != 5) { 
        freopen ("error_GR", "a", stderr);
        fprintf (stderr, "Number of parameters is incorrect!\n");
        return EXIT_FAILURE;
    }
    else {
     //  freopen (argv[4], "w", stderr);
       setbuf(stderr,NULL);
    }

    n = (unsigned int) strtol (argv[1], &tinp, 0);
    if ((*tinp != '\0') || (n >= MAXGROUPS )) { 
        fprintf (stderr, "Group process identification is wrong!\n");
        return EXIT_FAILURE;
    }
    strcpy (nFic, argv[2]);
    key = (unsigned int) strtol (argv[3], &tinp, 0);
    if (*tinp != '\0') { 
        fprintf (stderr, "Error on the access key communication!\n");
        return EXIT_FAILURE;
    }

    /* connection to the semaphore set and the shared memory region and mapping the shared region onto the
       process address space */
    if ((semgid = semConnect (key)) == -1) { 
        perror ("error on connecting to the semaphore set");
        return EXIT_FAILURE;
    }
    if ((shmid = shmemConnect (key)) == -1) { 
        perror ("error on connecting to the shared memory region");
        return EXIT_FAILURE;
    }
    if (shmemAttach (shmid, (void **) &sh) == -1) { 
        perror ("error on mapping the shared region on the process address space");
        return EXIT_FAILURE;
    }

    /* initialize random generator */
    srandom ((unsigned int) getpid ());                                                 


    /* simulation of the life cycle of the group */
    goToRestaurant(n);
    checkInAtReception(n);
    orderFood(n);
    waitFood(n);
    eat(n);
    checkOutAtReception(n);

    /* unmapping the shared region off the process address space */
    if (shmemDettach (sh) == -1) {
        perror ("error on unmapping the shared region off the process address space");
        return EXIT_FAILURE;;
    }

    return EXIT_SUCCESS;
}

/**
 *  \brief normal distribution generator with zero mean and stddev deviation. 
 *
 *  Generates random number according to normal distribution.
 * 
 *  \param stddev controls standard deviation of distribution
 */
static double normalRand(double stddev)
{
   int i;

   double r=0.0;
   for (i=0;i<12;i++) {
       r += random()/(RAND_MAX+1.0);
   }
   r -= 6.0;

   return r*stddev;
}

/**
 *  \brief group goes to restaurant 
 *
 *  The group takes its time to get to restaurant.
 *
 *  \param id group id
 */
static void goToRestaurant (int id)
{
    double startTime = sh->fSt.startTime[id] + normalRand(STARTDEV);
    
    if (startTime > 0.0) {
        usleep((unsigned int) startTime );
    }
}

/**
 *  \brief group eats
 *
 *  The group takes his time to eat a pleasant dinner.
 *
 *  \param id group id
 */
static void eat (int id)
{
    double eatTime = sh->fSt.eatTime[id] + normalRand(EATDEV);
    
    if (eatTime > 0.0) {
        usleep((unsigned int) eatTime );
    }
}

/**
 *  \brief group checks in at reception
 *
 *  Group should, as soon as receptionist is available, ask for a table,
 *  signaling receptionist of the request.  
 *  Group may have to wait for a table in this method.
 *  The internal state should be saved.
 *
 *  \param id group id
 *
 *  \return true if first group, false otherwise
 */
static void checkInAtReception(int id)
{
    bool isFirstGroup = false;

    if (semDown (semgid, sh->mutex) == -1) {                                                  /* enter critical region */
        perror ("error on the down operation for semaphore access (CT)");
        exit (EXIT_FAILURE);
    }


    // Check if this is the first group to check in
    if (sh->fSt.groupsWaiting == 0) {
        isFirstGroup = true;
    }
    sh->fSt.groupsWaiting++; // Increment the number of groups waiting
    sh->fSt.receptionistRequest.reqType = TABLEREQ; // Use the TABLEREQ for the check-in request
    sh->fSt.receptionistRequest.reqGroup = id;

    sh->fSt.st.groupStat[id] = ATRECEPTION; // Update group status to ATRECEPTION
    saveState(nFic, &sh->fSt); // Save the state

    
    // Determine table availability
    bool isTableAvailable = false;
    for (int i = 0; i < NUMTABLES; i++) { // Assuming NUMTABLES is the number of tables
        if (sh->fSt.assignedTable[i] == -1) { // -1 indicates the table is not assigned
            isTableAvailable = true;
            sh->fSt.assignedTable[i] = id; // Assign the table to the group
            sh->fSt.st.groupStat[id] = AT_TABLE;
            break;
        }
    }


    if (isTableAvailable) {
        sh->fSt.st.groupStat[id] = AT_TABLE; // Update group status to AT_TABLE if a table is available
    } else {
        sh->fSt.st.groupStat[id] = WAITING; // Update group status to WAITING if no table is available
    }
    saveState(nFic, &sh->fSt); // Save the state again


    if (semUp (semgid, sh->mutex) == -1) {                                                      /* exit critical region */
        perror ("error on the up operation for semaphore access (CT)");
        exit (EXIT_FAILURE);
    }

    return isFirstGroup;
}

/**
 *  \brief group orders food.
 *
 *  The group should update its state, request food to the waiter and 
 *  wait for the waiter to receive the request.
 *  
 *  The internal state should be saved.
 *
 *  \param id group id
 */
static void orderFood (int id)
{
    // TODO insert your code here

    if (semDown (semgid, sh->mutex) == -1) {                                                  /* enter critical region */
        perror ("error on the down operation for semaphore access (CT)");
        exit (EXIT_FAILURE);
    }

    // TODO insert your code here
    // Update group status to ORDERING
    sh->fSt.st.groupStat[id] = FOOD_REQUEST;
    saveState(nFic, &sh->fSt); // Save the state

    // Send order to the waiter
    // Assuming there is a structure like sh->fSt.waiterRequest
    sh->fSt.waiterRequest.reqType = FOODREQ;
    sh->fSt.waiterRequest.reqGroup = id;
    saveState(nFic, &sh->fSt); // Save the state again


    if (semUp (semgid, sh->mutex) == -1) {                                                     /* exit critical region */
        perror ("error on the up operation for semaphore access (CT)");
        exit (EXIT_FAILURE);
    }

    // TODO insert your code here
}

/**
 *  \brief group waits for food.
 *
 *  The group updates its state, and waits until food arrives. 
 *  It should also update state after food arrives.
 *  The internal state should be saved twice.
 *
 *  \param id group id
 */
static void waitFood (int id)
{
    if (semDown (semgid, sh->mutex) == -1) {                                                  /* enter critical region */
        perror ("error on the down operation for semaphore access (CT)");
        exit (EXIT_FAILURE);
    }

    // TODO insert your code here
    // Update group status to WAIT_FOR_FOOD
    sh->fSt.st.groupStat[id] = WAIT_FOR_FOOD;
    saveState(nFic, &sh->fSt); // Save the state

    if (semUp (semgid, sh->mutex) == -1) {                                                  /* enter critical region */
        perror ("error on the down operation for semaphore access (CT)");
        exit (EXIT_FAILURE);
    }


    // TODO insert your code here
    // Wait for the food to arrive
    // Assuming 'tableId' is known and valid for this group
    int tableId = sh->fSt.assignedTable[id];
    if (semDown(semgid, sh->foodArrived[tableId]) == -1) {
        perror("error on the down operation for food arrived semaphore (CT)");
        exit(EXIT_FAILURE);
    }


    if (semDown (semgid, sh->mutex) == -1) {                                                  /* enter critical region */
        perror ("error on the down operation for semaphore access (CT)");
        exit (EXIT_FAILURE);
    }


    // TODO insert your code here
    // Update group status to EAT
    sh->fSt.st.groupStat[id] = EAT;
    saveState(nFic, &sh->fSt); // Save the state again

    if (semUp (semgid, sh->mutex) == -1) {                                                  /* enter critical region */
        perror ("error on the down operation for semaphore access (CT)");
        exit (EXIT_FAILURE);
    }
}

/**
 *  \brief group check out at reception. 
 *
 *  The group, as soon as receptionist is available, updates its state and 
 *  sends a payment request to the receptionist.
 *  Group waits for receptionist to acknowledge payment. 
 *  Group should update its state to LEAVING, after acknowledge.
 *  The internal state should be saved twice.
 *
 *  \param id group id
 */
static void checkOutAtReception (int id)
{
    // TODO insert your code here

    if (semDown (semgid, sh->mutex) == -1) {                                                  /* enter critical region */
        perror ("error on the down operation for semaphore access (CT)");
        exit (EXIT_FAILURE);
    }

    // TODO insert your code here
    // Update group status to CHECKOUT
    sh->fSt.st.groupStat[id] = CHECKOUT;
    sh->fSt.receptionistRequest.reqType = BILLREQ; // BILLREQ for payment request
    sh->fSt.receptionistRequest.reqGroup = id;
    saveState(nFic, &sh->fSt); // Save the state


    if (semUp (semgid, sh->mutex) == -1) {                                                  /* enter critical region */
        perror ("error on the down operation for semaphore access (CT)");
        exit (EXIT_FAILURE);
    }

    // TODO insert your code here
    // Wait for receptionist to acknowledge the payment
    if (semDown(semgid, sh->tableDone[id]) == -1) { // Using tableDone semaphore for payment acknowledgment
        perror("error on the down operation for payment received semaphore (CT)");
        exit(EXIT_FAILURE);
    }


    if (semDown (semgid, sh->mutex) == -1) {                                                  /* enter critical region */
        perror ("error on the down operation for semaphore access (CT)");
        exit (EXIT_FAILURE);
    }

    // TODO insert your code here
    // Update group status to LEAVING
    sh->fSt.st.groupStat[id] = LEAVING;
    saveState(nFic, &sh->fSt); // Save the state again


    if (semUp (semgid, sh->mutex) == -1) {                                                  /* enter critical region */
        perror ("error on the down operation for semaphore access (CT)");
        exit (EXIT_FAILURE);
    }

}

