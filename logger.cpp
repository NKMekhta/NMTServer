#include "logger.h"

#include <errno.h>
#include <string.h>


Log::Log(std::ostream &_str) : str{_str}
{
	sem_init(&sem, 1, 1);
}


void Log::print(Type _t, int _cn, const char* _tx, int _errn)
{
	std::string out;
	switch (_t)
	{
	case INFO:
		out = "[INFO]";
		break;
	case WARNING:
		out = "[WARNING]";
		break;
	case ERROR:
		out = "[ERROR]";
		break;
	case FATAL:
		out = "[FATAL]";
		break;
	}
	out += "(CLINT=" + std::to_string(_cn) + ") ";
	out += _tx;
	if (_errn)
	{
		out += ": ";
		out += strerror(_errn);
	}
	sem_wait(&sem);
	str << out << std::endl;
	sem_post(&sem);
}


void Log::print(Type _t, const char* _who, const char* _tx, int _errn)
{
	std::string out;
	switch (_t)
	{
	case INFO:
		out = "[INFO]";
		break;
	case WARNING:
		out = "[WARNING]";
		break;
	case ERROR:
		out = "[ERROR]";
		break;
	case FATAL:
		out = "[FATAL]";
		break;
	}
	out += "(";
	out += _who;
	out += ") ";
	out += _tx;
	if (_errn)
	{
		out += ": ";
		out += strerror(_errn);
	}
	sem_wait(&sem);
	str << out << std::endl;
	sem_post(&sem);
}


void Log::print(Type _t, const char* _who, std::stringstream& _what, int _errn)
{
	std::string out;
	switch (_t)
	{
	case INFO:
		out = "[INFO]";
		break;
	case WARNING:
		out = "[WARNING]";
		break;
	case ERROR:
		out = "[ERROR]";
		break;
	case FATAL:
		out = "[FATAL]";
		break;
	}
	out += "(";
	out += _who;
	out += ") ";
	out += _what.str();
	_what.clear();
	if (_errn)
	{
		out += ": ";
		out += strerror(_errn);
	}
	sem_wait(&sem);
	str << out << std::endl;
	sem_post(&sem);
}
