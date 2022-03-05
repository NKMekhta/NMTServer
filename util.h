#pragma once

#include <map>
#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <sstream>
#include <cstring>
#include <iostream>
#include <algorithm>

#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <pthread.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define QUEUE_KEY 1111
#define SHM_KEY 2222
#define MAX_CONNECTIONS 100


void netServer();
void *clientInteractor(void *);
void imageServer();
void gameServer();


struct GameSession
{
	uint64_t clid[2];
	bool player = true;
	uint8_t left = 21;

	bool subtract(const uint8_t _amount);

	enum Errors {
		NOT_ENOUGH_STICKS = 1,
		GAME_ALREADY_WON,
		SUBTRACT_NOT_ENOUGH,
		SUBTRACT_TOO_MANY
	};
};


struct GameMsg
{
	uint64_t mtype;
	bool turn;
	uint8_t amount;
};


struct SharedSem
{
	sem_t clsem;
	sem_t svsem;
	bool proc;
	char comm[2048];
};
