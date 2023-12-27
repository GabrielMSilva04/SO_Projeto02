/**
 *  \file semSharedWaiter.c (implementation file)
 *
 *  \brief Problem name: Restaurant
 *
 *  Synchronization based on semaphores and shared memory.
 *  Implementation with SVIPC.
 *
 *  Definition of the operations carried out by the waiter:
 *     \li waitForClientOrChef
 *     \li informChef
 *     \li takeFoodToTable
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
#include <assert.h>

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

/** \brief waiter waits for next request */
static request waitForClientOrChef ();

/** \brief waiter takes food order to chef */
static void informChef(int group);

/** \brief waiter takes food to table */
static void takeFoodToTable (int group);




/**
 *  \brief Main program.
 *
 *  Its role is to generate the life cycle of one of intervening entities in the problem: the waiter.
 */
int main (int argc, char *argv[])
{
    int key;                                            /*access key to shared memory and semaphore set */
    char *tinp;                                                       /* numerical parameters test flag */

    /* validation of command line parameters */
    if (argc != 4) { 
        freopen ("error_WT", "a", stderr);
        fprintf (stderr, "Number of parameters is incorrect!\n");
        return EXIT_FAILURE;
    }
    else { 
        freopen (argv[3], "w", stderr);
        setbuf(stderr,NULL);
    }

    strcpy (nFic, argv[1]);
    key = (unsigned int) strtol (argv[2], &tinp, 0);
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

    /* simulation of the life cycle of the waiter */
    int nReq=0;
    request req;
    while( nReq < sh->fSt.nGroups*2 ) {
        puts("Começa a esperar");
        req = waitForClientOrChef();
        switch(req.reqType) {
            case FOODREQ:
                printf("Waiter received order from Group %d\n", req.reqGroup);
                informChef(req.reqGroup);
                puts("Waiter informed chef");
                break;
            case FOODREADY:
                printf("Comida de grupo %d pronta\n", req.reqGroup);
                takeFoodToTable(req.reqGroup);
                printf("Waiter served group %d", req.reqGroup);
                break;
        }
        nReq++;
    }

    /* unmapping the shared region off the process address space */
    if (shmemDettach (sh) == -1) {
        perror ("error on unmapping the shared region off the process address space");
        return EXIT_FAILURE;;
    }

    return EXIT_SUCCESS;
}

/**
 *  \brief waiter waits for next request 
 *
 *  Waiter updates state and waits for request from group or from chef, then reads request.
 *  The waiter should signal that new requests are possible.
 *  The internal state should be saved.
 *
 *  \return request submitted by group or chef
 */
static request waitForClientOrChef()
{
    request req;
    bool foundRequest = false;

    while (!foundRequest) {
        if (semDown(semgid, sh->mutex) == -1) {
            perror("error on the down operation for semaphore access (WT)");
            exit(EXIT_FAILURE);
        }

        // Check for group food requests
        for (int i = 0; i < MAXGROUPS; i++) {
            if (sh->fSt.st.groupStat[i] == FOOD_REQUEST) {
                req.reqType = FOODREQ;
                req.reqGroup = i;
                foundRequest = true;
                sh->fSt.st.groupStat[i] = WAIT_FOR_FOOD;
                break;
            }
        }

        // Check for chef's food ready signal
        if (!foundRequest && sh->fSt.st.chefStat == FOODREADY) {
            req.reqType = FOODREADY;
            req.reqGroup = sh->fSt.foodGroup;
            foundRequest = true;
        }

        if (semUp(semgid, sh->mutex) == -1) {
            perror("error on the up operation for semaphore access (WT)");
            exit(EXIT_FAILURE);
        }

        if (!foundRequest) {
            // Implement a wait or exit condition if no request is found
            // This could involve sleeping for a bit or exiting the loop after a timeout
            sh->fSt.st.waiterStat = WAIT_FOR_REQUEST;
        }
    }

    // Signal readiness for new requests
    if (semUp(semgid, sh->waiterRequestPossible) == -1) {
        perror("error on the up operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }

    return req;
}


/**
 *  \brief waiter takes food order to chef 
 *
 *  Waiter updates state and then takes food request to chef.
 *  Waiter should inform group that request is received.
 *  Waiter should wait for chef receiving request.
 *  The internal state should be saved.
 *
 */
static void informChef (int n)
{
    if (semDown (semgid, sh->mutex) == -1)  {                                                  /* enter critical region */
        perror ("error on the up operation for semaphore access (WT)");
        exit (EXIT_FAILURE);
    }


    // TODO insert your code here
    // Inform the chef of the new order
    sh->fSt.waiterRequest.reqGroup = n;  // Assuming waiterRequest is where the order info is stored
    sh->fSt.st.waiterStat = INFORM_CHEF;  // Update waiter's state to INFORM_CHEF
    saveState(nFic, &sh->fSt);  // Save the state

    
    if (semUp (semgid, sh->mutex) == -1)                                                   /* exit critical region */
    { perror ("error on the down operation for semaphore access (WT)");
        exit (EXIT_FAILURE);
    }

    
    // TODO insert your code here
    // Signal the chef that a new order is ready
    if (semUp(semgid, sh->waitOrder) == -1) {
        perror("error on the up operation for waitOrder semaphore (WT)");
        exit(EXIT_FAILURE);
    }

    // Wait for acknowledgement from the chef
    if (semDown(semgid, sh->orderReceived) == -1) {
        perror("error on the down operation for order received semaphore (WT)");
        exit(EXIT_FAILURE);
    }

}

/**
 *  \brief waiter takes food to table 
 *
 *  Waiter updates its state and takes food to table, allowing the meal to start.
 *  Group must be informed that food is available.
 *  The internal state should be saved.
 *
 */
static void takeFoodToTable (int n)
{
    if (semDown (semgid, sh->mutex) == -1)  {                                                  /* enter critical region */
        perror ("error on the up operation for semaphore access (WT)");
        exit (EXIT_FAILURE);
    }


    // TODO insert your code here
    // Update waiter's state (assuming there is a waiterStat field in the shared state)
    sh->fSt.st.waiterStat = TAKE_TO_TABLE;
    saveState(nFic, &sh->fSt); // Save the state

    
    if (semUp (semgid, sh->mutex) == -1)  {                                                  /* exit critical region */
        perror ("error on the down operation for semaphore access (WT)");
        exit (EXIT_FAILURE);
    }


    // Inform the group that the food is available (assuming the tableId is known for this group)
    int tableId = sh->fSt.assignedTable[n];
    if (semUp(semgid, sh->foodArrived[tableId]) == -1) {
        perror("error on the up operation for food arrived semaphore (WT)");
        exit(EXIT_FAILURE);
    }
}

