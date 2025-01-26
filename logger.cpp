#include "logger.h"

#include <errno.h>
#include <string.h>


Log::Log(std::ostream &_str) : str{_str}
{
	sem_init(&sem, 1, 1);
}


void Log::print(Type _t, uint64_t _cn, const char* _tx, int _errn)
{
	std::string out{ getType(_t) };
	out += "\t(CLINT=" + std::to_string(_cn) + ") " + _tx;
	if (_errn)
		out += std::string(": ") + strerror(_errn);
	sem_wait(&sem);
	str << out << std::endl;
	sem_post(&sem);
}


void Log::print(Type _t, uint64_t _who, std::stringstream& _what, int _errn)
{
	std::string out{ getType(_t) };
	out += "\t(CLINT=" + std::to_string(_who) + ") " + _what.str();
	if (_errn)
		out += std::string(": ") + strerror(_errn);
	sem_wait(&sem);
	str << out << std::endl;
	sem_post(&sem);

	_what.clear();
	_what.str({});
}



void Log::print(Type _t, const char* _who, const char* _tx, int _errn)
{
	std::string out{ getType(_t) };
	out += "\t(" + std::string(_who) + ") " + _tx;
	if (_errn)
		out += std::string(": ") + strerror(_errn);
	sem_wait(&sem);
	str << out << std::endl;
	sem_post(&sem);
}


void Log::print(Type _t, const char* _who, std::stringstream& _what, int _errn)
{
	std::string out{ getType(_t) };
	out += "\t(" + std::string(_who) + ") " + _what.str();
	if (_errn)
		out += std::string(": ") + strerror(_errn);
	sem_wait(&sem);
	str << out << std::endl;
	sem_post(&sem);

	_what.clear();
	_what.str({});
}


std::string Log::getType(const Type _t) const
{
	switch (_t)
	{
	case INFO:
		return "[INFO]";
	case WARNING:
		return "[WARN]";
	case ERROR:
		return "[ERROR]";
	case FATAL:
		return "[FATAL]";
	}
	return "";
}
