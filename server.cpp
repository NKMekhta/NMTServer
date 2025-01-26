#include "util.h"


int qid[2]{};
SharedSem* sem;
Log* logg;
sem_t thr_sync;

void netServer();
void *threadWrapper(void *);
void clientInteractor(int, uint64_t);
void imageServer();
void gameServer();
void sendWrapper(const uint64_t, const int, const void *, const uint64_t);
bool recvWrapper(const uint64_t, const int, void *, const uint64_t, bool = true);


int main()
{
	try
	{
		int shmid = shmget(SHM_KEY, sizeof(SharedSem), IPC_CREAT | 0777);
		if (shmid < 0)
			throw "[ERROR] shmget()";

		sem = (SharedSem *) shmat(shmid, NULL, 0);
		if (!sem)
			throw "[ERROR] shmat()";
		new (sem) SharedSem{ {}, {}, false, 0, "", Log(std::cout) };
		logg = &sem->log;
	}
	catch (const char *_err)
	{
		perror(_err);
		return 1;
	}

	pid_t child[3];
	uint8_t buff;
	qid[0] = msgget(QUEUE_KEY, IPC_CREAT | 0777);
	if (qid[0] < 0)
	{ logg->print(Log::ERROR, "ProcessManager", "msgget()", errno); return 1; }
	while (errno != ENOMSG)
		msgrcv(qid[0], &buff, 1, 0, IPC_NOWAIT);

	qid[1] = msgget(QUEUE_KEY + 1, IPC_CREAT | 0777);
	if (qid[1] < 0)
	{ logg->print(Log::ERROR, "ProcessManager", "msgget()", errno); return 1; }
	while (errno != ENOMSG)
		msgrcv(qid[1], &buff, 1, 0, IPC_NOWAIT);

	if (sem_init(&sem->clsem, 1, 1) || sem_init(&sem->svsem, 1, 1))
	{ logg->print(Log::ERROR, "ProcessManager", "sem_init()", errno); return 1; }

	child[0] = fork();
	if (child[0] < 0)
	{ logg->print(Log::ERROR, "ProcessManager", "fork()", errno); return 1; }
	if (child[0] == 0)
		netServer();
	logg->print(Log::INFO, "ProcessManager", "Forked NetServer.");

	child[1] = fork();
	if (child[1] < 0)
	{ logg->print(Log::ERROR, "ProcessManager", "fork()", errno); return 1; }
	if (child[1] == 0)
		gameServer();
	logg->print(Log::INFO, "ProcessManager", "Forked GameServer.");

	child[2] = fork();
	if (child[2] < 0)
	{ logg->print(Log::ERROR, "ProcessManager", "fork()", errno); return 1; }
	if (child[2] == 0)
		imageServer();
	logg->print(Log::INFO, "ProcessManager", "Forked ImageServer.");

	while (true) for (pid_t i : child)
	{
		sleep(1);
		if (waitpid(i, NULL, WNOHANG) >= 1)
		{
			logg->print(Log::FATAL, "ProcessManager", "Child process died, exiting...");
			kill(0, SIGTERM);
			return 1;
		}
	}
	return 0;
}


void netServer()
{
	std::stringstream logmsg;
	char addrbuff[INET_ADDRSTRLEN];
	int master_socket, nsocket;
	sem_init(&thr_sync, 1, 1);
	pthread_t thr;
	std::vector<int> active;
	std::vector<int>::reverse_iterator iter;


	if ((master_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		logg->print(Log::ERROR, "NetServer", "socket()", errno);
		exit(1);
	}

	sockaddr_in address;
	memset(&address, 0, sizeof(sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = 0;
	socklen_t addrlen = sizeof(address);

	if (bind(master_socket, (sockaddr *) &address, addrlen))
	{
		logg->print(Log::ERROR, "NetServer", "bind()", errno);
		exit(1);
	}

	if (getsockname(master_socket, (sockaddr *) &address, &addrlen))
	{
		logg->print(Log::ERROR, "NetServer", "getsockname()", errno);
		exit(1);
	}

	logmsg << "Bound to port " << ntohs(address.sin_port);
	logg->print(Log::INFO, "NetServer", logmsg);

	if (listen(master_socket, MAX_CONNECTIONS))
	{
		logg->print(Log::ERROR, "NetServer", "listen()", errno);
		exit(1);
	}

	while (true)
	{
		logg->print(Log::INFO, "NetServer", "Waiting for connections...");
        nsocket = accept(master_socket, (sockaddr *) &address, &addrlen);
		if (nsocket < 0)
		{
			logg->print(Log::ERROR, "NetServer", "listen()", errno);
			exit(1);
		}

		getnameinfo((sockaddr*) &address, addrlen, addrbuff, sizeof(addrbuff), 0, 0, NI_NUMERICHOST);
		logmsg << "Accepted connection from: " << addrbuff << ':' << ntohs(address.sin_port);
		logg->print(Log::INFO, "NetServer", logmsg);

		sem_wait(&thr_sync);
		for (iter = active.rbegin(); iter != active.rend() && !(*iter); ++iter);
		active.erase(iter.base(), active.end());
		active.push_back(nsocket);
		sem_post(&thr_sync);

		if (pthread_create(&thr, NULL, &threadWrapper, &active.back()))
		{
			logg->print(Log::ERROR, "NetServer", "pthread_create()", errno);
			exit(1);
		}
	}
}


void *threadWrapper(void *sockptr)
{
	sem_wait(&thr_sync);
	int my_sock = *(int *) sockptr;
	*(int *) sockptr = 0;
	sem_post(&thr_sync);

	uint64_t client_id = pthread_self();
	logg->print(Log::INFO, client_id, "Thread active.");

	try { clientInteractor(my_sock, client_id); }
	catch (...) { };

	if (close(my_sock) < 0)
		logg->print(Log::ERROR, client_id, "Can't close socket.");

	logg->print(Log::INFO, client_id, "Thread exited.");
	return NULL;
}


void clientInteractor(int my_socket, uint64_t client_id)
{
	namespace fs = std::filesystem;

	int err{};
	uint8_t type{};
	GameMsg qmsg{};
	std::stringstream logmsg;

	fs::path file;
	void *map_file{};
	char *recv_string{};
	int file_descriptor{};
	std::string name, command;
	uint64_t name_size{}, file_size{}, image_amount{};
	const std::string rm = "rm -rf ";
	const std::string mkdir = "mkdir -p ";
	const std::string imgdir = std::string("./images/") + std::to_string(client_id) + '/';

	while (true)
	{
		sleep(1);
		err = msgrcv(qid[1], &qmsg, sizeof(qmsg), client_id, IPC_NOWAIT);
		if (err >= 0)
		{
			logg->print(Log::INFO, client_id, "Forwarding message from game server.");
			if (send(my_socket, &qmsg.turn, sizeof(qmsg.amount) + sizeof(qmsg.turn), 0) < 0)
				return logg->print(Log::ERROR, client_id, "send()", errno);
		}
		else if (errno != ENOMSG)
			return logg->print(Log::ERROR, client_id, "msgrcv()", errno);

		if (!recvWrapper(client_id, my_socket, &type, sizeof(type), false))
			continue;
		logg->print(Log::INFO, client_id, "Recieved client message.");

		if (type)
		{
			logg->print(Log::INFO, client_id, "Started image processing.");
			recvWrapper(client_id, my_socket, &image_amount, sizeof(image_amount));

			logg->print(Log::INFO, client_id, (mkdir + imgdir).c_str());
			system((mkdir + imgdir).c_str());

			for (; image_amount; --image_amount)
			{
				recvWrapper(client_id, my_socket, &name_size,
							sizeof(name_size));
				recvWrapper(client_id, my_socket, &file_size,
							sizeof(file_size));
				recv_string = new char[name_size];
				recvWrapper(client_id, my_socket, recv_string, name_size);
				name = imgdir + recv_string;
				delete[] recv_string;

				std::ofstream(name.c_str());
				file_descriptor = open(name.c_str(), O_RDWR);
				if (file_descriptor < 0)
					return logg->print(Log::ERROR, client_id, "open()", errno);

				if (ftruncate(file_descriptor, file_size) < 0)
					return logg->print(Log::ERROR, client_id, "ftrunctuate()", errno);

				map_file = mmap(NULL, file_size, PROT_WRITE, MAP_SHARED, file_descriptor, 0);
				if (map_file == MAP_FAILED)
					return logg->print(Log::ERROR, client_id, "mmap()", errno);

				recvWrapper(client_id, my_socket, map_file, file_size);
				munmap(map_file, file_size);
				close(file_descriptor);
			}
			logg->print(Log::INFO, client_id, "All images received.");

			recvWrapper(client_id, my_socket, &name_size,
						sizeof(name_size));
			recv_string = new char[name_size];
			recvWrapper(client_id, my_socket, recv_string, name_size);
			command = recv_string;
			delete[] recv_string;

			recvWrapper(client_id, my_socket, &name_size, sizeof(name_size));
			recv_string = new char[name_size];
			recvWrapper(client_id, my_socket, recv_string, name_size);
			name = imgdir + recv_string;
			delete[] recv_string;

			logg->print(Log::INFO, client_id, "Waiting for image server entry...");
			sem_wait(&sem->clsem);

			logg->print(Log::INFO, client_id, "Entered image server.");
			sem_wait(&sem->svsem);
			sem->clint = client_id;
			memcpy(&sem->comm, command.c_str(), command.size() + 1);
			sem->proc = true;
			sem_post(&sem->svsem);

			logg->print(Log::INFO, client_id, "Waiting for image server result...");
			while (true)
			{
				sem_wait(&sem->svsem);
				if (!sem->proc)
					break;
				sem_post(&sem->svsem);
				sleep(1);
			}

			logg->print(Log::INFO, client_id, "Image processing finished.");
			file = fs::path(name);
			if (!fs::is_regular_file(file))
			{
				logg->print(Log::WARNING, client_id, "Image server processing error.");
				file_size = 0;
				sendWrapper(client_id, my_socket, &file_size, sizeof(file_size));
			}
			else
			{
				logg->print(Log::INFO, client_id, "Sending image result...");
				file_size = fs::file_size(file);

				file_descriptor = open(file.relative_path().c_str(), O_RDONLY);
				if (file_descriptor < 0)
					return logg->print(Log::ERROR, client_id, "open()", errno);

				map_file = mmap(NULL, file_size, PROT_READ, MAP_SHARED, file_descriptor, 0);
				if (map_file == MAP_FAILED)
					return logg->print(Log::ERROR, client_id, "mmap()", errno);

				sendWrapper(client_id, my_socket, &file_size, sizeof(file_size));
				sendWrapper(client_id, my_socket, map_file, file_size);

				munmap(map_file, file_size);
				close(file_descriptor);
			}

			logmsg << "Sent " << file_size << "B to client.";
			logg->print(Log::INFO, client_id, logmsg);

			sem_post(&sem->svsem);
			sem_post(&sem->clsem);

			logg->print(Log::INFO, client_id, (rm + imgdir).c_str());
			system((rm + imgdir).c_str());

			logg->print(Log::INFO, client_id, "Left image server.");
		}

		else
		{
			logg->print(Log::INFO, client_id, "Forwarding message to game server.");
			qmsg = { client_id, true, 0 };
			recvWrapper(client_id, my_socket, &qmsg.amount, sizeof(qmsg.amount));
			msgsnd(qid[0], &qmsg, sizeof(qmsg), 0);
		}
	}
}


void imageServer()
{
	std::string clidir;
	logg->print(Log::INFO, "ImageServer", "ImageServer running.");
	while (true)
	{
		sem_wait(&sem->svsem);
		if (sem->proc)
		{
			logg->print(Log::INFO, "ImageServer", "Processing request...");
			clidir = std::string("./images/") + std::to_string(sem->clint);
			logg->print(Log::INFO, sem->clint, (std::string("cd ") + clidir).c_str());
			chdir(clidir.c_str());
			logg->print(Log::INFO, sem->clint, sem->comm);
			system(sem->comm);
			sem->proc = false;
			logg->print(Log::INFO, sem->clint, "cd ../..");
			chdir("../..");
			logg->print(Log::INFO, "ImageServer", "Processing finished.");
		}
		sem_post(&sem->svsem);
		sleep(1);
	}
}


void gameServer()
{
	logg->print(Log::INFO, "GameServer", "Running.");
	std::stringstream logmsg;
	GameSession* session;
	GameMsg cmsg;
    uint64_t waiting[2] = { 0, 0 };
	using SessionID = std::pair<uint64_t, GameSession*>;
	std::map<uint64_t, GameSession*> sessions;

	while (true)
	{
		if (msgrcv(qid[0], &cmsg, sizeof(cmsg), 0, 0) < 0)
		{
			logg->print(Log::ERROR, "GameServer", "msgrcv()", errno);
			exit(1);
		}

		logmsg << "Received message from " << cmsg.mtype << '.';
		logg->print(Log::INFO, "GameServer", logmsg);

		if (sessions.count(cmsg.mtype))
		{
			session = sessions[cmsg.mtype];
			if ((cmsg.mtype == session->clid[0]) != session->player)
			{
				logmsg << "Unexpected message from " << cmsg.mtype << '.';
				logg->print(Log::WARNING, "GameServer", logmsg);
				continue;
			}

			try
			{
				session->subtract(cmsg.amount);
				logmsg << cmsg.mtype << " had a turn.";
				logg->print(Log::INFO, "GameServer", logmsg);
			}
			catch (const GameSession::Errors exc)
			{
				logmsg << cmsg.mtype << "\'s illegal turn rejected.";
				logg->print(Log::INFO, "GameServer", logmsg);

				cmsg = { cmsg.mtype, true, session->left };
				if (msgsnd(qid[1], &cmsg, sizeof(cmsg), 0))
				{
					logg->print(Log::ERROR, "GameServer", "msgsnd()", errno);
					exit(1);
				}
				logmsg << "Resent turn to " << cmsg.mtype << '.';
				logg->print(Log::INFO, "GameServer", logmsg);
				continue;
			}

			logmsg << "Sending game data to " << cmsg.mtype << '.';
			logg->print(Log::INFO, "GameServer", logmsg);
			cmsg = { session->clid[0], session->player, session->left };
			if (msgsnd(qid[1], &cmsg, sizeof(cmsg), 0))
			{
				logg->print(Log::ERROR, "GameServer", "msgsnd()", errno);
				exit(1);
			}


			logmsg << "Sending game data to " << cmsg.mtype << '.';
			logg->print(Log::INFO, "GameServer", logmsg);
			cmsg = { session->clid[1], !session->player, session->left };
			if (msgsnd(qid[1], &cmsg, sizeof(cmsg), 0))
			{
				logg->print(Log::ERROR, "GameServer", "msgsnd()", errno);
				exit(1);
			}

			if (!session->left)
			{
				logmsg << "Finishing game for " << session->clid[0];
				logmsg << " and " << session->clid[1] << '.';
				logg->print(Log::INFO, "GameServer", logmsg);

				sessions.erase(sessions.find(session->clid[0]));
				sessions.erase(sessions.find(session->clid[1]));
				delete session;
			}
		}

		else if (waiting[0])
		{
			waiting[1] = cmsg.mtype;
			logmsg << "Starting session for " << waiting[0];
			logmsg << " and " << waiting[1] << '.';
			logg->print(Log::INFO, "GameServer", logmsg);
			session = new GameSession;
			session->clid[0] = waiting[0];
			session->clid[1] = waiting[1];
			sessions.insert(SessionID(waiting[0], session));
			sessions.insert(SessionID(waiting[1], session));

			cmsg = { session->clid[0], session->player, session->left };
			logmsg << "Signaling start for " << cmsg.mtype << '.';
			logg->print(Log::INFO, "GameServer", logmsg);
			if (msgsnd(qid[1], &cmsg, sizeof(GameMsg), 0))
			{ logg->print(Log::ERROR, "GameServer", "msgsnd()", errno); exit(1); }

			cmsg = { session->clid[1], !session->player, session->left };
			logmsg << "Signaling start for " << cmsg.mtype << '.';
			logg->print(Log::INFO, "GameServer", logmsg);
			if (msgsnd(qid[1], &cmsg, sizeof(GameMsg), 0))
			{ logg->print(Log::ERROR, "GameServer", "msgsnd()", errno); exit(1); }

			waiting[0] = 0;
			waiting[1] = 0;
		}

		else
		{
            waiting[0] = cmsg.mtype;
            logmsg << "Adding " << waiting[0] << " to waiting room.";
			logg->print(Log::INFO, "GameServer", logmsg);
		}
	}
}


void GameSession::subtract(const uint8_t _amount)
{
	if (left < _amount)
		throw Errors::NOT_ENOUGH_STICKS;
	if (!left)
		throw Errors::GAME_ALREADY_WON;
	if (_amount < 1)
		throw Errors::SUBTRACT_NOT_ENOUGH;
	if (_amount > 3)
		throw Errors::SUBTRACT_TOO_MANY;

	left -= _amount;
	player = !player;
}


void sendWrapper(const uint64_t _who, const int _sockfd, const void *_ptr,
				 const uint64_t _len)
{
	if (send(_sockfd, _ptr, _len, 0) < 0)
	{
		if (errno == ECONNRESET)
			logg->print(Log::INFO, _who, "Client disconnected on send().");
		else
			logg->print(Log::ERROR, _who, "send()", errno);
		throw 0;
	}
}


bool recvWrapper(const uint64_t _who, const int _sockfd, void *_ptr,
				 const uint64_t _len, bool _wait)
{
	int err = recv(_sockfd, _ptr, _len, MSG_WAITALL | (MSG_DONTWAIT * !_wait));
	if (err > 0)
		return true;
	if (!err)
		logg->print(Log::INFO, _who, "Client disconnected on recv().");
	else if (errno == EAGAIN)
		return false;
	else
		logg->print(Log::ERROR, _who, "recv()", errno);
	throw 0;
}
