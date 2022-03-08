#pragma once

#include <iostream>

#include <sys/sem.h>
#include <sys/ipc.h>
#include <semaphore.h>


class Log
{
	std::ostream &str;
	sem_t sem;

public:
	Log(std::ostream &);
	~Log() = default;
	enum Type { INFO, WARNING, ERROR, FATAL } type;
	void print(Type, int, const char*, int = 0);
	void print(Type, const char*, const char*, int = 0);
};
