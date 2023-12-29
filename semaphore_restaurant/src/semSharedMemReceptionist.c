#define CHECKING_IN 0
#define AT_TABLE 1
#define WAITING 2

/**
 *  \file semSharedReceptionist.c (implementation file)
 *
 *  \brief Problem name: Restaurant
 *
 *  Synchronization based on semaphores and shared memory.
 *  Implementation with SVIPC.
 *
 *  Definition of the operations carried out by the receptionist:
 *     \li waitForGroup
 *     \li provideTableOrWaitingRoom
 *     \li receivePayment
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

/* constants for groupRecord */
#define TOARRIVE 0
#define WAIT     1
#define ATTABLE  2
#define DONE     3

/** \brief receptioninst view on each group evolution (useful to decide table binding) */
static int groupRecord[MAXGROUPS];


/** \brief receptionist waits for next request */
static request waitForGroup ();

/** \brief receptionist waits for next request */
static void provideTableOrWaitingRoom (int n);

/** \brief receptionist receives payment */
static void receivePayment (int n);



/**
 *  \brief Main program.
 *
 *  Its role is to generate the life cycle of one of intervening entities in the problem: the receptionist.
 */
int main (int argc, char *argv[])
{
    int key;                                            /*access key to shared memory and semaphore set */
    char *tinp;                                                       /* numerical parameters test flag */

    /* validation of command line parameters */
    if (argc != 4) { 
        freopen ("error_RT", "a", stderr);
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

    /* initialize internal receptionist memory */
    int g;
    for (g=0; g < sh->fSt.nGroups; g++) {
       groupRecord[g] = TOARRIVE;
    }

    /* simulation of the life cycle of the receptionist */
    int nReq=0;
    request req;
    while( nReq < sh->fSt.nGroups*2 ) {
        req = waitForGroup();
        switch(req.reqType) {
            case TABLEREQ:
                   provideTableOrWaitingRoom(req.reqGroup); //TODO param should be groupid
                   fprintf(stderr,"RT_%d: group %d went to table\n", getpid(), req.reqGroup);
                   break;
            case BILLREQ:
                   receivePayment(req.reqGroup);
                   fprintf(stderr,"RT_%d: group %d payed\n", getpid(), req.reqGroup);
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
 *  \brief decides table to occupy for group n or if it must wait.
 *
 *  Checks current state of tables and groups in order to decide table or wait.
 *
 *  \return table id or -1 (in case of wait decision)
 */
static int decideTableOrWait(int n)
{
    int tablesOccupied = 0;
    for (int tableId = 0; tableId < NUMTABLES; tableId++) {
        if (sh->fSt.assignedTable[tableId] == 1) {
            tablesOccupied++;
        }
    }
    if (tablesOccupied == NUMTABLES) {
        return -1;  // Indicate that the group must wait
    } else {
        // Find the first vacant table
        for (int tableId = 0; tableId < NUMTABLES; tableId++) {
            if (sh->fSt.assignedTable[tableId] == -1) {
                return tableId;  // Return the table ID
            }
        }
    }
    return -1;  // This should never be reached
}

/**
 *  \brief called when a table gets vacant and there are waiting groups 
 *         to decide which group (if any) should occupy it.
 *
 *  Checks current state of tables and groups in order to decide group.
 *
 *  \return group id or -1 (in case of wait decision)
 */
static int decideNextGroup()
{
    //TODO insert your code here
    if (semDown(semgid, sh->mutex) == -1) {  // enter critical region
        perror("error on the down operation for semaphore access (RT)");
        exit(EXIT_FAILURE);
    }

    int nextGroup = -1;
    // Iterate through the groups to find the first one waiting
    for (int i = 0; i < MAXGROUPS; i++) {
        if (groupRecord[i] == WAIT) {
            nextGroup = i;
            break; // Found the next group to be seated
        }
    }

    if (nextGroup != -1) {
        // If a waiting group is found, update its state
        groupRecord[nextGroup] = ATTABLE;
        sh->fSt.st.groupStat[nextGroup] = AT_TABLE;  // Update group status to AT_TABLE
    }

    if (semUp(semgid, sh->mutex) == -1) {  // exit critical region
        perror("error on the down operation for semaphore access (RT)");
        exit(EXIT_FAILURE);
    }

    return nextGroup; // Return the group ID or -1 if no group is waiting
}

/**
 *  \brief receptionist waits for next request 
 *
 *  Receptionist updates state and waits for request from group, then reads request,
 *  and signals availability for new request.
 *  The internal state should be saved.
 *
 *  \return request submitted by group
 */
static request waitForGroup()
{
    request req = {0};  // Initialize req to default values

    // Enter critical region
    if (semDown(semgid, sh->mutex) == -1) {
        perror("error on the down operation for semaphore access (RT)");
        exit(EXIT_FAILURE);
    }

    // Update receptionist state to WAIT and save the state
    sh->fSt.st.receptionistStat = WAIT;
    saveState(nFic, &sh->fSt);

    // Exit critical region
    if (semUp(semgid, sh->mutex) == -1) {
        perror("error on the up operation for semaphore access (RT)");
        exit(EXIT_FAILURE);
    }

    // Wait for a group to make a request
    // ...

    // Read the request from shared memory or another source
    // Make sure req is assigned a value before returning
    // ...

    return req;

}

/**
 *  \brief receptionist decides if group should occupy table or wait
 *
 *  Receptionist updates state and then decides if group occupies table
 *  or waits. Shared (and internal) memory may need to be updated.
 *  If group occupies table, it must be informed that it may proceed. 
 *  The internal state should be saved.
 *
 */
static void provideTableOrWaitingRoom (int n)
{
    if (semDown (semgid, sh->mutex) == -1)  {                                                  /* enter critical region */
        perror ("error on the up operation for semaphore access (WT)");
        exit (EXIT_FAILURE);
    }

    // TODO insert your code here
    // Decide if the group can be assigned a table or must wait
    int tableId = decideTableOrWait(n);  // This function should return a table ID or -1 if the group must wait

    if (tableId != -1) {
        // If a table is available
        // Update receptionist and group status
        sh->fSt.st.receptionistStat = ASSIGNTABLE;  // Assuming ASSIGNTABLE is a defined state for assigning table
        groupRecord[n] = ATTABLE;  // Update internal receptionist view

        // Signal the group that it can proceed to the table
        // Assuming 'waitForTable[n]' is the semaphore for the group to wait for a table
        if (semUp(semgid, sh->waitForTable[n]) == -1) {
            perror("error on the up operation for group wait for table semaphore (RT)");
            exit(EXIT_FAILURE);
        }
    } else {
        // If the group must wait
        // Update receptionist and group status
        sh->fSt.st.receptionistStat = WAIT;  // Assuming WAIT is a defined state for waiting
        groupRecord[n] = WAIT;  // Update internal receptionist view
    }

    saveState(nFic, &sh->fSt);  // Save the state


    if (semUp (semgid, sh->mutex) == -1) {                                               /* exit critical region */
        perror ("error on the down operation for semaphore access (WT)");
        exit (EXIT_FAILURE);
    }

}

/**
 *  \brief receptionist receives payment 
 *
 *  Receptionist updates its state and receives payment.
 *  If there are waiting groups, receptionist should check if table that just became
 *  vacant should be occupied. Shared (and internal) memory should be updated.
 *  The internal state should be saved.
 *
 */

static void receivePayment (int n)
{
    if (semDown (semgid, sh->mutex) == -1)  {                                                  /* enter critical region */
        perror ("error on the up operation for semaphore access (WT)");
        exit (EXIT_FAILURE);
    }

    // TODO insert your code here
    // Update receptionist state to receiving payment
    sh->fSt.st.receptionistStat = RECVPAY;  // Assuming RECVPAY is a defined state for receiving payment

    // Mark the table as vacant
    int tableId = sh->fSt.assignedTable[n];
    sh->fSt.assignedTable[tableId] = -1; // -1 indicates the table is now vacant
    groupRecord[n] = DONE;  // Update the internal receptionist view to indicate the group is done

    // Check if there are waiting groups
    int nextGroup = decideNextGroup();
    if (nextGroup != -1) {
        // If there is a waiting group, assign the newly vacant table to it
        sh->fSt.assignedTable[tableId] = nextGroup;
        groupRecord[nextGroup] = ATTABLE;  // Update the internal receptionist view
        sh->fSt.st.groupStat[nextGroup] = AT_TABLE;  // Update group status to AT_TABLE
        if (semUp(semgid, sh->waitForTable[nextGroup]) == -1) {
            perror("error on the up operation for group wait for table semaphore (RT)");
            exit(EXIT_FAILURE);
        }
    }

    saveState(nFic, &sh->fSt);  // Save the state

    if (semUp (semgid, sh->mutex) == -1)  {                                                  /* exit critical region */
     perror ("error on the down operation for semaphore access (WT)");
        exit (EXIT_FAILURE);
    }

    // TODO insert your code here
}

