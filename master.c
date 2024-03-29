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
#include <math.h>
#include <sys/time.h>

#include "lib/config.h"
#include "lib/utilities.h"
#include "lib/msgPortProtocol.h"
#include "lib/customMacro.h"

#include "lib/master.h"

const int HOUR_IN_DAY = 24;

int NUM_OF_SETTINGS = 16;
int *configArr = 0;

int configSharedMemoryId = 0;
int portSharedMemoryId = 0;
int boatSharedMemoryId = 0;

int goodSharedMemoryId = 0;
int *dailyLotDistributionArr = 0;
InitGoods *initGoodsArr = 0;
sem_t *initGoodsSemaphore = 0;

int goodAnalyzerSharedMemoryId;
int boatAnalyzerSharedMemoryId;
int portAnalyzerSharedMemoryId;
int endgoodAnalyzerSharedMemoryId;
int acknowledgeDumpSharedMemoryId;

int acknowledgeInitSharedMemoryId = 0;
int *acknowledgeInitArr = 0;

int analyzerReadingMsgQueue;
int analyzerWritingMsgQueue;

int currentProcessId;
int simulationFinishedEarly = 0;

void handle_master_stopProcess() {

    /* Block all incoming signals after the first SIGINT */
    sigset_t mask;
    sigfillset(&mask);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    printConsole("\nStopping program...");
    killpg(getpid(), SIGINT);
    killpg(getpid(), SIGCONT);
    cleanup();
    exit(0);
}

int main() {

    int analyzerArgs[9];
    int weatherArgs[4];
    int portArgs[8];
    int boatArgs[8];

    printConsole("Starting program...");

    /* ----- CONFIG ----- */
    configSharedMemoryId = generateSharedMemory(sizeof(int) * NUM_OF_SETTINGS);
    if (configSharedMemoryId == -1) {
        safeExit(1);
    }

    configArr = (int*) shmat(configSharedMemoryId, NULL, 0);
    if (configArr == (void*) -1) {
        safeExit(2);
    }

    initializeEnvironment();
    initializeSingalsHandlers();
    if (loadConfig(configSharedMemoryId) == -1) {
        safeExit(3);
    }
     
     
    /* ----- ALL ----- */
    acknowledgeInitSharedMemoryId = 
        generateSharedMemory(sizeof(int) * (configArr[SO_NAVI] + configArr[SO_PORTI] + 2)); /* +2 analyzer, weather */
    if (acknowledgeInitSharedMemoryId == -1) {
        safeExit(10);
    }

    acknowledgeInitArr = (int*) shmat(acknowledgeInitSharedMemoryId, NULL, 0);
    if (acknowledgeInitArr == (void*) -1) {
        handleErrno("shmat()");
        return -1;
    }

    acknowledgeDumpSharedMemoryId = generateSharedMemory(sizeof(int) * (configArr[SO_NAVI] + configArr[SO_PORTI]));
    if (acknowledgeDumpSharedMemoryId == -1) {
        safeExit(19);
    }


    /* ----- ANALYZER ----- */
    analyzerReadingMsgQueue = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    analyzerWritingMsgQueue = msgget(IPC_PRIVATE, IPC_CREAT | 0600);

    goodAnalyzerSharedMemoryId = generateSharedMemory(
        sizeof(goodDailyDump) * configArr[SO_MERCI] * (configArr[SO_NAVI] + configArr[SO_PORTI]));
    if (goodAnalyzerSharedMemoryId == -1) {
        safeExit(4);
    }

    boatAnalyzerSharedMemoryId = generateSharedMemory(sizeof(boatDailyDump) * configArr[SO_NAVI]);
    if (boatAnalyzerSharedMemoryId == -1) {
        safeExit(5);
    }

    portAnalyzerSharedMemoryId = generateSharedMemory(sizeof(portDailyDump) * configArr[SO_PORTI]);
    if (portAnalyzerSharedMemoryId == -1) {
        safeExit(6);
    }

    endgoodAnalyzerSharedMemoryId = generateSharedMemory(sizeof(goodEndDump) * configArr[SO_MERCI]);
    if (endgoodAnalyzerSharedMemoryId == -1) {
        safeExit(17);
    }

    if (generateSemaphore(endgoodAnalyzerSharedMemoryId) == (void*) -1) {
        handleError("Error during creation of semaphore for goods end dump");
        safeExit(18);
    }

    debug("Start creating the analyzer...");
    analyzerArgs[0] = configSharedMemoryId;
    analyzerArgs[1] = goodAnalyzerSharedMemoryId;
    analyzerArgs[2] = boatAnalyzerSharedMemoryId;
    analyzerArgs[3] = portAnalyzerSharedMemoryId;
    analyzerArgs[4] = analyzerReadingMsgQueue;
    analyzerArgs[5] = analyzerWritingMsgQueue;
    analyzerArgs[6] = endgoodAnalyzerSharedMemoryId;
    analyzerArgs[7] = acknowledgeInitSharedMemoryId;
    analyzerArgs[8] = acknowledgeDumpSharedMemoryId;
    if (generateSubProcesses(1, "./bin/analyzer", 0, analyzerArgs, sizeof(analyzerArgs) / sizeof(analyzerArgs[0])) == -1) {
        safeExit(7);
    }


    /* ----- GOODS ----- */
    goodSharedMemoryId = generateSharedMemory(sizeof(Goods) * configArr[SO_MERCI]);
    if (goodSharedMemoryId == -1) {
        safeExit(8);
    }

    initGoodsArr = (InitGoods*) shmat(goodSharedMemoryId, NULL, 0);
    if (initGoodsArr == (void*) -1) {
        handleErrno("shmat()");
        safeExit(23);
    }

    if (generateSemaphore(goodSharedMemoryId) == (void*) -1) {
        handleError("Error during creation of semaphore for goods");
        safeExit(18);
    }

    dailyLotDistributionArr = (int*) malloc(sizeof(int) * configArr[SO_DAYS]);
    generateSubgroupSums(dailyLotDistributionArr, floor((double) configArr[SO_FILL] / configArr[SO_SIZE]), configArr[SO_DAYS]);

    if (generateGoods(0) == -1) {
        safeExit(9);
    }


    /* ----- PORTS ----- */
    portSharedMemoryId = generateSharedMemory(sizeof(Port) * configArr[SO_PORTI]);
    if (portSharedMemoryId == -1) {
        safeExit(11);
    }

    debug("Start creating the ports...");
    portArgs[0] = configSharedMemoryId;
    portArgs[1] = portSharedMemoryId;
    portArgs[2] = goodSharedMemoryId;
    portArgs[3] = goodAnalyzerSharedMemoryId;
    portArgs[4] = portAnalyzerSharedMemoryId;
    portArgs[5] = acknowledgeInitSharedMemoryId;
    portArgs[6] = endgoodAnalyzerSharedMemoryId;
    portArgs[7] = acknowledgeDumpSharedMemoryId;
    if (generateSubProcesses(configArr[SO_PORTI], "./bin/porto", 1, portArgs, sizeof(portArgs) / sizeof(portArgs[0])) == -1) {
        safeExit(12);
    }


    /* ----- BOATS ----- */
    boatSharedMemoryId = generateSharedMemory(sizeof(Boat) * configArr[SO_NAVI]);
    if (boatSharedMemoryId == -1) {
        safeExit(20);
    }

    debug("Start creating the boats...");
    boatArgs[0] = configSharedMemoryId;
    boatArgs[1] = portSharedMemoryId;
    boatArgs[2] = goodAnalyzerSharedMemoryId;
    boatArgs[3] = boatAnalyzerSharedMemoryId;
    boatArgs[4] = acknowledgeInitSharedMemoryId;
    boatArgs[5] = endgoodAnalyzerSharedMemoryId;
    boatArgs[6] = acknowledgeDumpSharedMemoryId;
    boatArgs[7] = boatSharedMemoryId;
    if (generateSubProcesses(configArr[SO_NAVI], "./bin/nave", 1, boatArgs, sizeof(boatArgs) / sizeof(boatArgs[0])) == -1) {
        safeExit(13);
    }


    /* ----- WEATHER ----- */
    debug("Start creating the weather...");
    weatherArgs[0] = configSharedMemoryId;
    weatherArgs[1] = boatSharedMemoryId;
    weatherArgs[2] = portSharedMemoryId;
    weatherArgs[3] = acknowledgeInitSharedMemoryId;
    if (generateSubProcesses(1, "./bin/meteo", 0, weatherArgs, sizeof(weatherArgs) / sizeof(weatherArgs[0])) == -1) {
        safeExit(22);
    }

    /* Init mask after fork to avoid to copy master options */
    initializeSingalsMask();

    /* ----- START SIMULATION ----- */
    if (work() == -1) {
        safeExit(15);
    }

    if (cleanup() == -1) {
        safeExit(16);
    }

    return 0;
}

void initializeSingalsMask() {

    sigset_t sigMask;

    /* Mask all signals except SIGINT */
    sigfillset(&sigMask);
    sigdelset(&sigMask, SIGINT);
    sigprocmask(SIG_SETMASK, &sigMask, NULL);

    /* If i don't set the group it will not register the ctrl + c in the console */
    setpgid(getpid(), getppid());
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
        handleError("Error during waiting response from PA_FINISH");
        return -1;
    }

    switch (response.msg.data.action)
    {
        case PA_FINISH:
            break;
        case PA_EOS_GSR:
            printConsole("No more good requested or in stock");
            simulationFinishedEarly = 1;
            return checkForAnalizerToFinish();
            break;
        case PA_EOS_ABS:
            printConsole("No more boats alive");
            simulationFinishedEarly = 1;
            return checkForAnalizerToFinish();
            break;
        default:
            handleError("Wrong action response instead of PA_FINISH");
            return -1;
    }

    return 0;
}

int waitForAnalizerToCollectData() {

    PortMessage response;

    int msgResponse = receiveMessage(analyzerReadingMsgQueue, &response, 0, 0);
    if (msgResponse == -1) {
        handleError("Error during waiting response from PA_DATA_COL");
        return -1;
    }

    if (response.msg.data.action != PA_DATA_COL) {
        handleError("Wrong action response instead of PA_DATA_COL");
        return -1;
    }

    return 0;
}

int acknowledgeChildrenStatus(int checkAll) {

    double waitTimeInSeconds = 0.005;
    int allChildrenInit, i;
    int entities = configArr[SO_NAVI] + configArr[SO_PORTI];

    if (checkAll == 1) {
        entities++; /* Analyzer */
        entities++; /* Weather */
    }

    do
    {
        long waitTimeInNs = getNanoSeconds(waitTimeInSeconds);
        if (safeWait(0, waitTimeInNs) == -1) {
            handleError("Error while waiting init children");
            return -1;
        }

        allChildrenInit = 1;

        for (i = 0; i < entities; i++)
        {
            if (acknowledgeInitArr[i] == 0) {
                debug("Failed acknowledge check");
                allChildrenInit = 0;
                break;
            }
        }

        waitTimeInSeconds += waitTimeInSeconds;

        if (waitTimeInSeconds > 1) {

#ifdef DEBUG
            for (i = 0; i < entities; i++)
            {
                char buffer[128];
                char *entety;

                if (i < configArr[SO_PORTI]) {
                    entety = "P";
                } else if (i >= configArr[SO_PORTI] && i < (configArr[SO_PORTI] + configArr[SO_NAVI])) {
                    entety = "B";
                } else if (i == configArr[SO_PORTI] + configArr[SO_NAVI]) {
                    entety = "A";
                } else {
                    entety = "W";
                }

                snprintf(buffer, sizeof(buffer), "(%s) AcknowledgeInitArr[%d] = %d", entety, i, acknowledgeInitArr[i]);
                debug(buffer);
            }
#endif

            handleError("Wait time for acknowledge check exeaded 1 second");
            return -1;
        }

    } while (allChildrenInit != 1);

    /* Reset acknowledge */
    for (i = 0; i < entities; i++)
    {
        if (acknowledgeInitArr[i] != -1) {
            acknowledgeInitArr[i] = 0;
        }
    }

    return 0;
}

int work() {

    int simulationDays = 0;
    int hourTimeSpan = getNanoSeconds((double) 1 / HOUR_IN_DAY);

    if (acknowledgeChildrenStatus(1) == -1) {
        handleError("Error while waiting for children to be initialized");
        return -1;
    }

#ifdef DEBUG
    if (1 == 1) { /* If i don't use the if it return a warning for the init of i */
        int i;
        char buffer[128];
        Port *portArr;
        Boat *boatArr;

        portArr = (Port*) shmat(portSharedMemoryId, NULL, 0);
        boatArr = (Boat*) shmat(boatSharedMemoryId, NULL, 0);

        for (i = 0; i < configArr[SO_PORTI]; i++)
        {
            snprintf(buffer, sizeof(buffer), "(%d) Port[%d]->pid = %d", i, i, portArr[i].pid);
            debug(buffer);
        }
        
        for (i = 0; i < configArr[SO_NAVI]; i++)
        {
            snprintf(buffer, sizeof(buffer), "(%d) Boat[%d]->pid = %d", configArr[SO_PORTI] + i, i, boatArr[i].pid);
            debug(buffer);
        }

        shmdt(portArr);
        shmdt(boatArr);
    }
#endif

    /* Start simulation [boat, port, analyzer] */
    killpg(getpid(), SIGCONT);
    debug("Sended SIGCONT");

    while (simulationDays < configArr[SO_DAYS] && simulationFinishedEarly == 0)
    {
        char buffer[128];
        int currentHour = 0;

#ifdef DEBUG
        struct timeval startTime, finishTime;
        double elapsedTimeInMs;

        gettimeofday(&startTime, NULL);
#endif

#ifdef DEBUG
        snprintf(buffer, sizeof(buffer), "||| ------------------- Day number %d ------------------- |||", simulationDays);
#else
        snprintf(buffer, sizeof(buffer), "Starting day %d", simulationDays);
#endif
        printConsole(buffer);

        simulationDays++;

        /* Wait for the day (1 second) */
        do
        {
            if (safeWait(0l, hourTimeSpan) == -1) {
                handleError("Error while waiting next day master");
                return -1;
            }
            /* Passed 1 hour */
            killpg(getpid(), SIGPOLL);

            currentHour++;

        } while (currentHour != HOUR_IN_DAY);

        if (simulationDays < configArr[SO_DAYS] && simulationFinishedEarly == 0) {

            /* Stop all */
            killpg(getpid(), SIGUSR1);
            killpg(getpid(), SIGCONT);

            debug("Sended stop all the process");

            if (acknowledgeChildrenStatus(0) == -1) {
                handleError("Error while waiting for children to stop");
                return -1;
            }

            /* Wait fo the analyzer to finish the job and check if simulation must end earlier */
            checkForAnalizerToFinish();

            /* Dump the data [boat, port] */
            killpg(getpid(), SIGUSR1);
            killpg(getpid(), SIGCONT);
            debug("Sended dump the data");

            if (acknowledgeChildrenStatus(0) == -1) {
                handleError("Error while waiting for children to dump the data");
                return -1;
            }
            
            if (sendMessage(analyzerWritingMsgQueue, PA_NEW_DAY, -1, -1) == -1) {
                handleError("Error during sendig of the PA_NEW_DAY");
                return -1;
            }

            /* Wait fo the boats and ports to dump data [analyzer] */
            waitForAnalizerToCollectData();

            /* Generate new goods */
            if (generateGoods(simulationDays) == -1) {
                handleError("Error while generating goods");
                return -1;
            }

            /* Contiue the simulation */
            killpg(getpid(), SIGUSR1);
            killpg(getpid(), SIGCONT);
            debug("Sended contiue the simulation");

            if (acknowledgeChildrenStatus(0) == -1) {
                handleError("Error while waiting for children to contiue the simulation");
                return -1;
            }
        }

#ifdef DEBUG
        gettimeofday(&finishTime, NULL);

        elapsedTimeInMs = (finishTime.tv_sec - startTime.tv_sec) * 1000.0;
        elapsedTimeInMs += (finishTime.tv_usec - startTime.tv_usec) / 1000.0;
        
        snprintf(buffer, sizeof(buffer), "Day: %d finish in %f ms", simulationDays, elapsedTimeInMs);
        debug(buffer);
#endif
    }

    killpg(getpid(), SIGUSR2);
    debug("Sended signal finish simulation");

    if (simulationFinishedEarly == 0) {
        if (sendMessage(analyzerWritingMsgQueue, PA_NEW_DAY, -1, -1) == -1) {
            handleError("Error during sendig of the PA_NEW_DAY finish");
            return -1;
        }
    }

#ifdef DEBUG
        printConsole("||| ------------------- Simulation finished ------------------- |||");
#else
        printConsole("Simulation finished");
#endif

    if (acknowledgeChildrenStatus(1) == -1) {
        handleError("Error while waiting for children to finish");
        return -1;
    }

    return 0;
}

int generateSharedMemory(int sizeOfSegment) {
    
    int *sharedMemory;
    int sharedMemoryId = shmget(IPC_PRIVATE, sizeOfSegment, 0600);
    if (sharedMemoryId == -1) {
        handleErrno("shmget()");
        return -1;
    }

    sharedMemory = (int*) shmat(sharedMemoryId, NULL, 0);
    if (sharedMemory == (void*) -1) {
        handleErrno("shmat()");
        return -1;
    }

    memset(sharedMemory, 0, sizeOfSegment);

    if (shmdt(sharedMemory) == -1) {
        handleErrno("shmdt()");
        return -1;
    }

    return sharedMemoryId;
}

/* Generate a semaphore */
/* https://stackoverflow.com/questions/32205396/share-posix-semaphore-among-multiple-processes */
sem_t *generateSemaphore(int semKey) {

    char semaphoreKey[12];
    sem_t *semaphore;

    if (sprintf(semaphoreKey, "%d", semKey) == -1) {
        handleError("Error during conversion of the pid for semaphore to a string");
        return (sem_t*) -1;
    }   

    semaphore = sem_open(semaphoreKey, O_CREAT, 0600, 1);
    if (semaphore == SEM_FAILED) {
        handleErrno("sem_open()");
        return (sem_t*) -1;
    }

    return semaphore;
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
            handleError("Error during conversion of the argument to a string");
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
                handleError("Error during conversion of the id to a string");
                return -1;
            }

            memcpy(args[0], key, sizeof(char) * 12); 
            currentProcessId++;
        }

        if (id == -1) {
            handleError("Error during fork of the file");
            return -1;
        }
        if (id == 0) {  
            if (execve(execFilePath, args, NULL) == -1) {
                handleErrno("execve()");
                return -1;
            }
        } 
    }

    free(args);

    return 0;
}

/* Generate all the goods requested and initialize them */
int generateGoods(int dayOfSimulation) {

    int *randomGoodsDistribution = 0;
    int randomPortNumber = getRandomValue(1, configArr[SO_PORTI]);
    int i, j, expiredGoods = 0, currentGoodsNumber = 0;

    /* Count how many goods are expired, to avoid refill them after first day */
    if (dayOfSimulation != 0) {
        for (i = 0; i < configArr[SO_MERCI]; i++) {
            if (initGoodsArr[i].good.remaningDays <= 0) {
                expiredGoods++;
            }
        }
    }

    currentGoodsNumber = configArr[SO_MERCI] - expiredGoods;

    randomGoodsDistribution = (int*) malloc(sizeof(int) * currentGoodsNumber);
    memset(randomGoodsDistribution, 0, sizeof(int) * currentGoodsNumber);

    /* TODO attualemente anche se una merce scade noi andiamo ad assegnarle un valore che ovviamente non verrà mai usato */
    generateSubgroupSums(randomGoodsDistribution, dailyLotDistributionArr[dayOfSimulation], currentGoodsNumber);

    j = 0;
    for (i = 0; i < configArr[SO_MERCI]; i++) {
        
        InitGoods initGoods;
        int lotsToSet = randomGoodsDistribution[j];
        
        /* Check first initialization */
        if (dayOfSimulation == 0) {

            initGoods.good.id = i;
            initGoods.good.goodLots = lotsToSet;
            initGoods.good.state = Undefined;
            initGoods.good.remaningDays = getRandomValue(configArr[SO_MIN_VITA], configArr[SO_MAX_VITA]);

            initGoods.spareLot = floor((double) lotsToSet / configArr[SO_PORTI]) + 1;
            initGoods.spareLotAmmount = lotsToSet % configArr[SO_PORTI];
            initGoods.defaultLot = floor((double) lotsToSet / configArr[SO_PORTI]);
            initGoods.defaultLotAmmount = configArr[SO_PORTI] - (lotsToSet % configArr[SO_PORTI]);

            /* Go the the next value for the lots */
            j++;
        } else {
            
            initGoods.good = initGoodsArr[i].good;
            
            if (initGoods.good.remaningDays > 0) {
                
                initGoods.good.goodLots = lotsToSet;
                initGoods.good.remaningDays--;
                
                initGoods.spareLot = floor((double) lotsToSet / randomPortNumber) + 1;
                initGoods.spareLotAmmount = lotsToSet % randomPortNumber;
                initGoods.defaultLot = floor((double) lotsToSet / randomPortNumber);
                initGoods.defaultLotAmmount = randomPortNumber - (lotsToSet % randomPortNumber);

                /* Go the the next value for the lots */
                j++;
            } else {
                
                initGoods.good.goodLots = 0;

                initGoods.spareLot = 0;
                initGoods.spareLotAmmount = 0;
                initGoods.defaultLot = 0;
                initGoods.defaultLotAmmount = 0;
            }
        }

        initGoodsArr[i] = initGoods;
    }

    if (currentGoodsNumber != 0) {
        free(randomGoodsDistribution);
    }

#ifdef DEBUG
    if (1 == 1) {
        char buffer[128];
        int totalLotsDistributed = 0;

        for (i = 0; i < configArr[SO_MERCI]; i++)
        {
            snprintf(buffer, sizeof(buffer), "Good %d: %d ammount d: %d s: %d", i, initGoodsArr[i].good.goodLots, initGoodsArr[i].defaultLotAmmount, initGoodsArr[i].spareLotAmmount);
            debug(buffer);
            totalLotsDistributed += initGoodsArr[i].good.goodLots;
        }

        snprintf(buffer, sizeof(buffer), "Lots distributed: %d/%d for %d", totalLotsDistributed, dailyLotDistributionArr[dayOfSimulation], randomPortNumber);
        debug(buffer);
    }
#endif

    return 0;
}

int cleanup() {

    free(dailyLotDistributionArr);

    if (configArr != 0 && shmdt(configArr) == -1) {
        handleErrno("shmdt()");
        return -1;
    }

    if (configSharedMemoryId != 0 && shmctl(configSharedMemoryId, IPC_RMID, NULL) == -1) {
        handleErrno("shmctl()");
        return -1;
    }

    if (initGoodsArr != 0 && shmdt(initGoodsArr) == -1) {
        handleErrno("shmdt()");
        return -1;
    }

    if (goodSharedMemoryId != 0 && shmctl(goodSharedMemoryId, IPC_RMID, NULL) == -1) {
        handleErrno("shmctl()");
        return -1;
    }

    if (initGoodsSemaphore != 0 && sem_close(initGoodsSemaphore) == -1) {
        handleErrno("sem_close()");
        return -1;
    }
    
    if (portSharedMemoryId != 0 && shmctl(portSharedMemoryId, IPC_RMID, NULL) == -1) {
        handleErrno("shmctl()");
        return -1;
    }

    if (boatSharedMemoryId != 0 && shmctl(boatSharedMemoryId, IPC_RMID, NULL) == -1) {
        handleErrno("shmctl()");
        return -1;
    }

    if (acknowledgeInitArr != 0 && shmdt(acknowledgeInitArr) == -1) {
        handleErrno("shmdt()");
        return -1;
    }

    if (acknowledgeInitArr != 0 && shmctl(acknowledgeInitSharedMemoryId, IPC_RMID, NULL) == -1) {
        handleErrno("shmctl()");
        return -1;
    }

    debug("Master clean");

    return 0;
}

void safeExit(int exitNumber) {

    cleanup();
    killpg(getpid(), SIGINT);
    exit(exitNumber);
}