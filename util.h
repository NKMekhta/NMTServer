#pragma once

#include "logger.h"

#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <cstdint>
#include <sstream>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <filesystem>

#include <fcntl.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#define QUEUE_KEY 1111
#define SHM_KEY 2222
#define MAX_CONNECTIONS 100


struct GameSession
{
	uint64_t clid[2];
	bool player{true};
	uint8_t left{21};

	void subtract(const uint8_t);

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
	uint64_t clint;
	char comm[2048];
	Log log;
};
