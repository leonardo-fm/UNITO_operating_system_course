#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>   
#include <sys/shm.h>  
#include <sys/msg.h>

#include <semaphore.h>
#include <fcntl.h>      

#include "lib/config.h"
#include "lib/utilities.h"
#include "lib/msgPortProtocol.h"

int NUM_OF_SETTINGS = 13;
int* configArr;
 
Port port;
GoodExchange goodExchange;

int main(int argx, char* argv[]) {

    initializeEnvironment();

    if (initializeConfig(argv[0]) == -1) {
        printf("Initialization of port config failed\n");
        exit(1);
    }

    if (initializePort(argv[1], argv[2], argv[3]) == -1) {
        printf("Initialization of port %s failed\n", argv[1]);
        exit(2);
    }

    if (cleanup() == -1) {
        printf("Cleanup failed\n");
        exit(3);
    }

    return 0;
}

/* Recover the array of the configurations values */
int initializeConfig(char* configShareMemoryIdString) {

    char* p;
    int configShareMemoryId = strtol(configShareMemoryIdString, &p, 10);
    configArr = (int*) shmat(configShareMemoryId, NULL, 0);
    if (configArr == (void*) -1) {
        return -1;
    }

    return 0;
}

int initializePort(char* portIdString, char* portShareMemoryIdS, char* goodShareMemoryIdS) {

    if (initializePortStruct(portIdString, portShareMemoryIdS) == -1) {
        printf("Error occurred during init of port struct\n");
        return -1;
    }

    if (initializeExchangeGoods() == -1) {
        printf("Error occurred during init of goods exchange\n");
        return -1;
    }

    if (initializePortGoods(goodShareMemoryIdS) == -1) {
        printf("Error occurred during init of goods\n");
        return -1;
    }

    return 0;
}

int initializePortStruct(char* portIdString, char* portShareMemoryIdS) {
    
    /* Generate a message queue to comunicate with the boats */
    int portMsgId = msgget(IPC_PRIVATE, 0600);
    if (portMsgId == -1) {
        return -1;
    }
    
    char* p;
    int portId = strtol(portIdString, &p, 10);

    port.id = portId;
    port.msgQueuId = portMsgId;
    if (port.id < 4) {
        port.position = getCornerCoordinates(configArr[SO_LATO], configArr[SO_LATO], port.id);
    } else {
        port.position = getRandomCoordinates(configArr[SO_LATO], configArr[SO_LATO]);
    }
    port.quays = getRandomValue(4, configArr[SO_BANCHINE]);
    port.availableQuays = port.quays;

    int shareMemoryId = strtol(portShareMemoryIdS, &p, 10);
    Coordinates* arr = (Coordinates*) shmat(shareMemoryId, NULL, 0);
    if (arr == (void*) -1) {
        return -1;
    }

    arr[port.id] = port.position;

    return 0;
}

int initializeExchangeGoods() {

    int i = 0;

    goodExchange.exchange = malloc(2 * sizeof(int*));
    if (goodExchange.exchange == NULL) {
        printf("Error during initialize exchange 0\n");
        return -1;
    }

    for (i = 0; i < 2; i++) {
        goodExchange.exchange[i] = malloc(sizeof(int) * configArr[SO_MERCI]);
        if (goodExchange.exchange[i] == NULL) {
            printf("Error during initialize exchange 1\n");
            return -1;
        }
    }

    int maxRequest = (configArr[SO_FILL] / 2) / configArr[SO_MERCI] / configArr[SO_PORTI];
    for (i = 0; i < configArr[SO_MERCI]; i++) {
        goodExchange.exchange[0][i] = 0;
        goodExchange.exchange[1][i] = maxRequest;
    }
}

int initializePortGoods(char* goodShareMemoryIdS) {

    char semaphoreKey[12];
    if (sprintf(semaphoreKey, "%d", getppid()) == -1) {
        printf("Error during conversion of the pid for semaphore to a string\n");
        return -1;
    }   

    sem_t *semaphore = sem_open(semaphoreKey, O_EXCL, 0600, 1);
    if (semaphore == SEM_FAILED) {
        printf("port failed to found semaphore with key %s\n", semaphoreKey);
        return -1;
    }

    /* Generate an array of random number with no repetitions */
    int numOfGoods = configArr[SO_MERCI] / 2;
    int goodsToTake[numOfGoods];
    int i, j = 0;
    for (i = 0; i < numOfGoods; i++) {
        int flag = 1;
        int randomValue;
        while (flag == 1) {
            flag = 0;
            randomValue = getRandomValue(0, (configArr[SO_MERCI] - 1));
            for (j = 0; j < numOfGoods; j++) {
                if (goodsToTake[j] == randomValue) {
                    flag = 1;
                    break;
                }
            }
        }

        goodsToTake[i] = randomValue;
    }

    char* p;
    int shareMemoryId = strtol(goodShareMemoryIdS, &p, 10);
    Goods* arr = (Goods*) shmat(shareMemoryId, NULL, 0);
    if (arr == (void*) -1) {
        printf("Error opening goods shared memory\n");
        return -1;
    }

    int maxTake = (configArr[SO_FILL] / 2) / ((configArr[SO_MERCI] / 2) - 1) / configArr[SO_PORTI];

    sem_wait(semaphore);

    for (i = 0; i < numOfGoods; i++) {
        int currentGood = goodsToTake[i];

        if (arr[currentGood].loadInTon == 0) {
            continue;
        }

        if (arr[currentGood].loadInTon < maxTake) {
            goodExchange.exchange[0][currentGood] += arr[currentGood].loadInTon;
            arr[currentGood].loadInTon = 0;
        } else {
            arr[currentGood].loadInTon -= maxTake;
            goodExchange.exchange[0][currentGood] += maxTake;
        }

        /* Set to 0 the request for the current good in the port */
        goodExchange.exchange[1][currentGood] = 0;
    }

    sem_post(semaphore);

    if (sem_close(semaphore) < 0) {
        printf("Error unable to close the good semaphore\n");
        return -1;
    }

    return 0;
}

int cleanup() {
    
    if (msgctl(port.msgQueuId, IPC_RMID, NULL) == -1) {
        printf("The queue failed to be closed\n");
        return -1;
    }
}