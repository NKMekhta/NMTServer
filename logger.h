#pragma once

#include <sstream>
#include <iomanip>
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
	std::string getType(const Type) const;
	void print(Type, uint64_t, const char*, int = 0);
	void print(Type, const char*, const char*, int = 0);
	void print(Type, const char*, std::stringstream&, int = 0);
	void commDebug(const char*, const char*, std::stringstream&);
	void commDebug(uint64_t, const char*, std::stringstream&);
	void commDebug(const char*, uint64_t, std::stringstream&);
};
