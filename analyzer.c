#define _GNU_SOURCES

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <errno.h>
#include <string.h>

#include <linux/limits.h>

#include "lib/analyzer.h"

int *configArr;

int goodAnalyzerSharedMemoryId;
int boatAnalyzerSharedMemoryId; 
int portAnalyzerSharedMemoryId;

char *logPath;

int currentDay = 0;
int simulationRunning = 1;

void handle_analyzer_simulation_signals(int signal) {

    switch (signal)
    {
        case SIGUSR1:
            break;

        /* Wait for the new day to come */
        case SIGUSR2:
            printf("Analyzer recived SIGUSR2\n");
            break;

        case SIGCONT:
            break;

        /* End of the simulation */
        case SIGSYS:
            simulationRunning = 0;
            break;
        default:
            printf("Intercept a unhandled signal: %d\n", signal);
            break;
    }
}

void handle_analyzer_stopProcess() {
    printf("Stopping analyzer...\n");
    cleanup();
    exit(0);
}

/* argv[0]=id | argv[1]=ganalizersh | argv[2]=banalyzersh | argv[3]=panalyzersh */
int main(int argx, char *argv[]) {

    (void) argx;
    initializeSingalsHandlers();

    if (initializeConfig(argv[0]) == -1) {
        printf("Initialization of analyzer config failed\n");
        exit(1);
    }

    if (initializeAnalyzer(argv[1], argv[2], argv[3]) == -1) {
        printf("Initialization of analyzer failed\n");
        exit(2);
    }

    if (work() == -1) {
        printf("Error during analyzer work\n");
        exit(3);
    }

    if (cleanup() == -1) {
        printf("Analyzer cleanup failed\n");
        exit(4);
    }

    return 0;
}

int initializeSingalsHandlers() {

    struct sigaction signalAction;

    setpgid(getpid(), getppid());

    signal(SIGUSR1, handle_analyzer_simulation_signals);

    /* Use different method because i need to use the handler multiple times */
    signalAction.sa_flags = SA_RESTART;
    signalAction.sa_handler = &handle_analyzer_simulation_signals;
    sigaction(SIGUSR2, &signalAction, NULL);

    signal(SIGCONT, handle_analyzer_simulation_signals);
    signal(SIGSYS, handle_analyzer_simulation_signals);
    signal(SIGINT, handle_analyzer_stopProcess);

    return 0;
}

int initializeConfig(char *configShareMemoryIdString) {

    char *p;
    int configShareMemoryId = strtol(configShareMemoryIdString, &p, 10);
    
    configArr = (int*) shmat(configShareMemoryId, NULL, 0);
    if (configArr == (void*) -1) {
        printf("The config key as failed to be conveted in analyzer\n");
        return -1;
    }

    return 0;
}

int initializeAnalyzer(char *goodAnalyzerShareMemoryIdString, char *boatAnalyzerShareMemoryIdString, char *portAnalyzerShareMemoryIdString) {

    char *p;

    goodAnalyzerSharedMemoryId = strtol(goodAnalyzerShareMemoryIdString, &p, 10);
    boatAnalyzerSharedMemoryId = strtol(boatAnalyzerShareMemoryIdString, &p, 10);
    portAnalyzerSharedMemoryId = strtol(portAnalyzerShareMemoryIdString, &p, 10);

    createLogFile();

    return 0;
}

int createLogFile() {
    char currentWorkingDirectory[PATH_MAX];
    FILE *filePointer;

    if (getcwd(currentWorkingDirectory, sizeof(currentWorkingDirectory)) == NULL) {
        printf("Error retreavin working directory\n");
        return -1;
    }

    logPath = malloc(PATH_MAX);
    sprintf(logPath, "%s/out/dump.txt", currentWorkingDirectory);

    filePointer = fopen(logPath, "w");
    if (filePointer == NULL) {
        if (errno == ENOENT) {

            /* File does not exist, create it */
            filePointer = fopen(logPath, "w");
            if (filePointer == NULL) {
                printf("Error creating file: %s\n", strerror(errno));
                return -1;
            }
        } else {
            printf("Error opening file: %s\n", strerror(errno));
            return -1;
        }
    } else { 
        
        /* File exist, erase old data */
        if (ftruncate(fileno(filePointer), 0) != 0) {
            printf("Error truncating file: %s\n", strerror(errno));
            return -1;
        }    
    }

    if (fclose(filePointer) != 0) {
        printf("Error closing file: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

int waitForNewDay() {

    int sig, waitRes;
    sigset_t sigset;

    sigaddset(&sigset, SIGUSR2);
    waitRes = sigwait(&sigset, &sig);

    return waitRes;
}

int work() {

    FILE *filePointer = fopen(logPath, "a");

    while (simulationRunning == 1)
    {
        printf("Start analyzer waiting...\nListening in group %d\n", getpgrp());
        waitForNewDay();
        printf("Finished analyzer waiting...\n");

        checkDataDump();

        if (filePointer == NULL) {
            printf("Error opening file\n");
            return -1;
        }

        if (generateDailyHeader(filePointer) == -1) {
            printf("Error while writing header\n");
            return -1;
        }

        if (generateDailyGoodReport(filePointer) == -1) {
            printf("Error while writing good report\n");
            return -1;
        }

        if (generateDailyBoatReport(filePointer) == -1) {
            printf("Error while writing boat report\n");
            return -1;
        }

        if (generateDailyPortReport(filePointer) == -1) {
            printf("Error while writing port report\n");
            return -1;
        }

        currentDay++;

        kill(getppid(), SIGALRM); 
    }

    if (fclose(filePointer) != 0) {
        printf("Error closing file: %s\n", strerror(errno));
        return -1;
    }
    
    return 0;
}

int checkDataDump() {

    /* Controlla che tutti abbiano scirtto */

    kill(getppid(), SIGTTIN);  

    return 0;
}

int generateDailyHeader(FILE *filePointer) {

    fprintf(filePointer, "\t\t=====================\t\t\\nDay: %d\n", currentDay);

    return 0;
}

int generateDailyGoodReport(FILE *filePointer) {

    return 0;
}

int generateDailyBoatReport(FILE *filePointer) {

    return 0;
}

int generateDailyPortReport(FILE *filePointer) {

    return 0;
}

int cleanup() { 

    free(logPath);

    if (shmctl(goodAnalyzerSharedMemoryId, IPC_RMID, NULL) == -1) {
        printf("The good shared memory failed to be closed in analyzer\n");
        return -1;
    }
    
    if (shmctl(boatAnalyzerSharedMemoryId, IPC_RMID, NULL) == -1) {
        printf("The boat shared memory failed to be closed in analyzer\n");
        return -1;
    }

    if (shmctl(portAnalyzerSharedMemoryId, IPC_RMID, NULL) == -1) {
        printf("The port shared memory failed to be closed in analyzer\n");
        return -1;
    }

    return 0;
}