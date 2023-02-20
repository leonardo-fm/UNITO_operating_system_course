#define _GNU_SOURCES

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <semaphore.h>
#include <fcntl.h>    
#include <sys/shm.h>    
#include <signal.h> 
#include <sys/msg.h>

#include "lib/config.h"
#include "lib/utilities.h"
#include "lib/msgPortProtocol.h"

#include "lib/master.h"

int configShareMemoryId;
int goodShareMemoryId;
int portShareMemoryId;

int goodAnalyzerShareMemoryId;
int boatAnalyzerShareMemoryId;
int portAnalyzerShareMemoryId;

int analyzerReadingMsgQueue;
int analyzerWritingMsgQueue;

int NUM_OF_SETTINGS = 13;
int *configArr;
int currentProcessId;

void handle_master_stopProcess() { 

    printf("\nStopping program...\n");
    killpg(getpid(), SIGINT);
    cleanup();
    exit(0);
}

int main() {

    int analyzerArgs[6];
    int portArgs[5];
    int boatArgs[4];

    setpgid(getpid(), getpid());

    /* ----- CONFIG ----- */
    configShareMemoryId = generateShareMemory(sizeof(int) * NUM_OF_SETTINGS);
    if (configShareMemoryId == -1) {
        printf("Error during creation of shared memory for config\n");
        safeExit(1);
    }

    configArr = (int*) shmat(configShareMemoryId, NULL, 0);
    if (configArr == (void*) -1) {
        safeExit(2);
    }

    initializeEnvironment();
    initializeSingalsHandlers();
    if (loadConfig(configShareMemoryId) == -1) {
        safeExit(3);
    }

    /* ----- ANALYZER ----- */
    analyzerReadingMsgQueue = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    analyzerWritingMsgQueue = msgget(IPC_PRIVATE, IPC_CREAT | 0600);

    goodAnalyzerShareMemoryId = generateShareMemory(
        sizeof(goodDailyDump) * configArr[SO_MERCI] * (configArr[SO_NAVI] + configArr[SO_PORTI]));
    if (goodAnalyzerShareMemoryId == -1) {
        printf("Error during creation of shared memory for good analyzer\n");
        safeExit(4);
    }

    boatAnalyzerShareMemoryId = generateShareMemory(sizeof(boatDailyDump) * configArr[SO_NAVI]);
    if (boatAnalyzerShareMemoryId == -1) {
        printf("Error during creation of shared memory for boat analyzer\n");
        safeExit(5);
    }

    portAnalyzerShareMemoryId = generateShareMemory(sizeof(portDailyDump) * configArr[SO_PORTI]);
    if (portAnalyzerShareMemoryId == -1) {
        printf("Error during creation of shared memory for port analyzer\n");
        safeExit(6);
    }

    analyzerArgs[0] = configShareMemoryId;
    analyzerArgs[1] = goodAnalyzerShareMemoryId;
    analyzerArgs[2] = boatAnalyzerShareMemoryId;
    analyzerArgs[3] = portAnalyzerShareMemoryId;
    analyzerArgs[4] = analyzerReadingMsgQueue;
    analyzerArgs[5] = analyzerWritingMsgQueue;
    if (generateSubProcesses(1, "./bin/analyzer", 0, analyzerArgs, 6) == -1) {
        safeExit(7);
    }


    /* ----- GOODS ----- */
    goodShareMemoryId = generateShareMemory(sizeof(Goods) * configArr[SO_MERCI]);
    if (goodShareMemoryId == -1) {
        printf("Error during creation of shared memory for goods\n");
        safeExit(8);
    }

    if (initializeGoods(goodShareMemoryId) == -1) {
        safeExit(9);
    }


    /* ----- PORTS ----- */
    portShareMemoryId = generateShareMemory(sizeof(Port) * configArr[SO_PORTI]);
    if (portShareMemoryId == -1) {
        printf("Error during creation of shared memory for ports\n");
        safeExit(10);
    }

    portArgs[0] = configShareMemoryId;
    portArgs[1] = portShareMemoryId;
    portArgs[2] = goodShareMemoryId;
    portArgs[3] = goodAnalyzerShareMemoryId;
    portArgs[4] = portAnalyzerShareMemoryId;
    if (generateSubProcesses(configArr[SO_PORTI], "./bin/porto", 1, portArgs, 5) == -1) {
        safeExit(11);
    }


    /* ----- BOATS ----- */
    boatArgs[0] = configShareMemoryId;
    boatArgs[1] = portShareMemoryId;
    boatArgs[2] = goodAnalyzerShareMemoryId;
    boatArgs[3] = boatAnalyzerShareMemoryId;
    if (generateSubProcesses(configArr[SO_NAVI], "./bin/nave", 1, boatArgs, 4) == -1) {
        safeExit(12);
    }

    /* TODO removit */
    sleep(1);

    /* ----- START SIMULATION ----- */
    if (work() == -1) {
        printf("Error during master work\n");
        safeExit(13);
    }

    if (cleanup() == -1) {
        printf("Cleanup failed\n");
        safeExit(14);
    }

    return 0;
}

int initializeSingalsHandlers() {

    setpgrp();

    signal(SIGINT, handle_master_stopProcess);

    return 0;
}

int checkForAnalizerToFinish() {

    PortMessage response;

    int msgResponse = receiveMessage(analyzerReadingMsgQueue, &response, 0, 0);
    if (msgResponse == -1) {
        printf("Error during waiting response from PA_FINISH\n");
        return -1;
    }

    if (response.msg.data.action != PA_FINISH) {
        printf("Wrong action response instead of PA_FINISH, get %d\n", response.msg.data.action);
        return -1;
    }

    return 0;
}

int waitForAnalizerToCollectData() {

    PortMessage response;

    int msgResponse = receiveMessage(analyzerReadingMsgQueue, &response, 0, 0);
    if (msgResponse == -1) {
        printf("Error during waiting response from PA_DATA_COL\n");
        return -1;
    }

    if (response.msg.data.action != PA_DATA_COL) {
        printf("Wrong action response instead of PA_DATA_COL, get %d\n", response.msg.data.action);
        return -1;
    }

    return 0;
}

int work() {

    int simulationDays = 0;

    /* Set the current group different from itselfe to avoid interupt from custom signals */
    setpgid(getpid(), getppid());

    killpg(getpid(), SIGUSR1);

    while (simulationDays < configArr[SO_DAYS])
    {
        /* Salta il primo giorno */
        if (simulationDays > 0) {
            checkForAnalizerToFinish();
        }

        printf("Day number %d\n", simulationDays);
        simulationDays++;

        if (safeWait(1, 0l) == -1) {
            printf("Error while waiting next day master\n");
            return -1;
        }

        if (simulationDays < configArr[SO_DAYS]) {

            killpg(getpid(), SIGUSR2);

            if (sendMessage(analyzerWritingMsgQueue, PA_NEW_DAY, -1, -1) == -1) {
                printf("Error during sendig of the PA_NEW_DAY\n");
                return -1;
            }

            waitForAnalizerToCollectData();

            killpg(getpid(), SIGCONT);
        }
    }

    killpg(getpid(), SIGSYS); /*Si svegliano? essendo che i porti e navi stano aspettando un SIGCONT*/

    if (sendMessage(analyzerWritingMsgQueue, PA_NEW_DAY, -1, -1) == -1) {
        printf("Error during sendig of the PA_NEW_DAY finish\n");
        return -1;
    }

    printf("Simulation finished\n");

    return 0;
}

int generateShareMemory(int sizeOfSegment) {
    int shareMemoryId = shmget(IPC_PRIVATE, sizeOfSegment, 0600);
    if (shareMemoryId == -1) {
        printf("Error during creation of the shared memory\n");
        return -1;
    }

    return shareMemoryId;
}

/* Generate processes by forking master and using execve */
/* Return 0 if the processes has been loaded succesfully, -1 if some errors occurred. */
int generateSubProcesses(int nOfProcess, char *execFilePath, int includeProceduralId, int *arguments, int argSize) {
    
    int i, arraySize;    
    char **args;

    if(nOfProcess <= 0) {
        return 0;
    }

    arraySize = argSize + includeProceduralId + 1;
    args = malloc(sizeof(char *) * arraySize);
    for (i = 0; i < arraySize; i++) {
        args[i] = (char *) malloc(sizeof(char) * 12);
    }

    for (i = 0 + includeProceduralId; i < argSize + includeProceduralId; i++) {
        
        char key[12];
    
        if (sprintf(key, "%d", arguments[i - includeProceduralId]) == -1) {
            printf("Error during conversion of the argument %d° to a string\n", i);
            return -1;
        }

        memcpy(args[i], key, sizeof(char) * 12); 
    }
 
    args[arraySize - 1] = '\0';

    i = 0;
    while (i++ < nOfProcess)
    {
        int id = fork();
        
        /* If includeProceduralId == 1 set in the first position the id of the process */
        if (includeProceduralId == 1) {

            char key[12];
        
            if (sprintf(key, "%d", currentProcessId) == -1) {
                printf("Error during conversion of the id to a string\n");
                return -1;
            }

            memcpy(args[0], key, sizeof(char) * 12); 
            currentProcessId++;
        }

        if (id == -1) {
            printf("Error during fork of the file %s\n", execFilePath);
            return -1;
        }
        if (id == 0) {  
            if (execve(execFilePath, args, NULL) == -1) {
                printf("Error during innesting of the file %s\n", execFilePath);
                return -1;
            }
        } 
    }

    free(args);

    return 0;
}

int initializeGoods(int goodShareMemoryId) {

    /* Init of goods */
    if (generateGoods(goodShareMemoryId) == -1) {
        return -1;
    }

    /* Init of semaphore */
    if (generateSemaphore() == -1) {
        return -1;
    }

    return 0;
}

/* Generate all the goods requested and initialize them */
int generateGoods(int goodShareMemoryId) {

    int goodQuantity, i;

    Goods *arr = (Goods*) shmat(goodShareMemoryId, NULL, 0);
    if (arr == (void*) -1) {
        printf("Error during good memory allocation\n");
        return -1;
    }

    goodQuantity = (int)((configArr[SO_FILL] / 2) / configArr[SO_MERCI]);
    for (i = 0; i < configArr[SO_MERCI]; i++) {
        Goods good;
        good.id = i;
        good.loadInTon = goodQuantity;
        good.state = Undefined;
        good.remaningDays = getRandomValue(configArr[SO_MIN_VITA], configArr[SO_MAX_VITA]);

        arr[i] = good;
    }

    if (shmdt(arr) == -1) {
        printf("The init good arr detach failed\n");
        return -1;
    }

    return 0;
}

/* Generate a semaphore */
/* https://stackoverflow.com/questions/32205396/share-posix-semaphore-among-multiple-processes */
int generateSemaphore() {

    sem_t *semaphore;

    char semaphoreKey[12];
    if (sprintf(semaphoreKey, "%d", getpid()) == -1) {
        printf("Error during conversion of the pid for semaphore to a string\n");
        return -1;
    }   

    semaphore = sem_open(semaphoreKey, O_CREAT, 0600, 1);
    if (semaphore == SEM_FAILED) {
        printf("Error on opening the semaphore\n");
        return -1;
    }

    if (sem_close(semaphore) < 0) {
        printf("Error on closing the semaphore\n");
        return -1;
    }

    return 0;
}

int cleanup() {
    printf("Master clean\n");

    if (shmdt(configArr) == -1) {
        printf("The config detach failed\n");
        return -1;
    }

    if (shmctl(configShareMemoryId, IPC_RMID, NULL) == -1) {
        printf("The shared memory failed to be closed\n");
        return -1;
    }

    if (shmctl(goodShareMemoryId, IPC_RMID, NULL) == -1) {
        printf("The shared memory failed to be closed\n");
        return -1;
    }
    
    if (shmctl(portShareMemoryId, IPC_RMID, NULL) == -1) {
        printf("The shared memory failed to be closed\n");
        return -1;
    }

    return 0;
}

void safeExit(int exitNumber) {
    cleanup();
    printf("Program %s exit with error %d\n", __FILE__, exitNumber);
    killpg(getpid(), SIGINT);
    exit(exitNumber);
}