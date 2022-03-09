#include "util.h"


int qid[2];
SharedSem* sem;
pthread_t mthread;
std::vector<pthread_t> active;
Log* logg;

int main()
{
	try {
		int shmid = shmget(SHM_KEY, sizeof(SharedSem), IPC_CREAT | 0777);
		if (shmid < 0)
			throw "[ERROR] shmget()";

		sem = (SharedSem *) shmat(shmid, NULL, 0);
		if (!sem)
			throw "[ERROR] shmat()";
		new (sem) SharedSem{ {}, {}, false, "", Log(std::cout) };
		logg = &sem->log;
	}
	catch (const char *_err)
	{
		perror(_err);
		return 1;
	}

	pid_t child[3];
	mthread = pthread_self();

	qid[0] = msgget(ftok("inqueue", QUEUE_KEY), IPC_CREAT | 0777);
	if (qid[0] < 0)
	{ logg->print(Log::ERROR, "ProcessManager", "msgget()", errno); return 1; }

	qid[1] = msgget(ftok("outqueue", QUEUE_KEY), IPC_CREAT | 0777);
	if (qid[1] < 0)
	{ logg->print(Log::ERROR, "ProcessManager", "msgget()", errno); return 1; }


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
	if (child[1] != 0)
		gameServer();
	logg->print(Log::INFO, "ProcessManager", "Forked GameServer.");

	child[2] = fork();
	if (child[2] < 0)
	{ logg->print(Log::ERROR, "ProcessManager", "fork()", errno); return 1; }
	if (child[2] == 0)
		imageServer();
	logg->print(Log::INFO, "ProcessManager", "Forked ImageServer.");


	pid_t result;
	while (true) for (pid_t i : child)
	{
		result = waitpid(i, NULL, WNOHANG);
		if (result < 1)
			continue;
		else
		{
			logg->print(Log::FATAL, "ProcessManager", "Child process died, exiting...");
			for (pid_t j : child)
				kill(j, SIGTERM);
			return 1;
		}
		sleep(5);
	}
	return 0;
}


void netServer()
{
	std::stringstream logmsg;
	auto thr_iter = active.cbegin();
	int master_socket, nsocket;
	if ((master_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{ logg->print(Log::ERROR, "NetServer", "socket()", errno); exit(1); }

	sockaddr_in address;
	memset(&address, 0, sizeof(sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = 0;
	uint32_t addrlen = sizeof(address);

	if (bind(master_socket, (sockaddr *) &address, addrlen))
	{ logg->print(Log::ERROR, "NetServer", "bind()", errno); exit(1); }

	if (getsockname(master_socket, (sockaddr *) &address, &addrlen))
	{ logg->print(Log::ERROR, "NetServer", "getsockname()", errno); exit(1); }

	logmsg << "Bound to port " << ntohs(address.sin_port) << '.';
	logg->print(Log::INFO, "NetServer", logmsg);

	if (listen(master_socket, MAX_CONNECTIONS))
	{ logg->print(Log::ERROR, "NetServer", "listen()", errno); exit(1); }

	while (true)
	{
		logg->print(Log::INFO, "NetServer", "Waiting for connections...");
        nsocket = accept(master_socket, (sockaddr *) &address, &addrlen);
		if (nsocket < 0)
		{ logg->print(Log::ERROR, "NetServer", "listen()", errno); exit(1); }

		logg->print(Log::INFO, "NetServer", "Accepted connection.");
		thr_iter = std::find(active.cbegin(), active.cend(), mthread);
		if (thr_iter == active.cend())
		{
			active.push_back(mthread);
			thr_iter = --active.cend();
		}
		if (pthread_create((pthread_t*) &(*thr_iter), NULL, &clientInteractor, &nsocket))
		{ logg->print(Log::ERROR, "NetServer", "pthread_create()", errno); exit(1); }
	}
}


void *clientInteractor(void *msock)
{
	int err, my_socket = *(int *) msock;
	auto my_iter = std::find(active.begin(), active.end(), pthread_self());
	uint64_t client_id = 1 + (my_iter - active.cbegin());
	logg->print(Log::INFO, client_id, "Thread active.");

	uint8_t type;
	GameMsg qmsg;
	uint64_t in_size = 0;
	using DataSize = std::pair<uint64_t, uint64_t>;
	std::vector<DataSize> datsiz;
	auto datsiz_iter = datsiz.cbegin();
	char *data;
	std::string img_name;
	std::ofstream img_file;
	std::ifstream img_outfile;

	while (true)
	{
		while (true)
		{
			err = msgrcv(qid[1], &qmsg, sizeof(qmsg), client_id, IPC_NOWAIT);
			if (errno == ENOMSG || !err)
				break;
			if (err < 0)
			{ logg->print(Log::ERROR, client_id, "msgrcv()", errno); pthread_exit(NULL); }

			logg->print(Log::INFO, client_id, "Forwarding messages from game server.");
			if (send(my_socket, &qmsg.amount, sizeof(qmsg.amount), 0) < 0 ||
					send(my_socket, &qmsg.turn, sizeof(qmsg.turn), 0) < 0)
			{ logg->print(Log::ERROR, client_id, "send()", errno); pthread_exit(NULL); }
			logg->print(Log::INFO, client_id, "Forwarding finished.");
		}

		err = recv(my_socket, &type, sizeof(type), MSG_DONTWAIT);
		if (err < 0)
		{
			if (errno == EAGAIN)
				continue;
			else
			{ logg->print(Log::ERROR, client_id, "recv().", errno); pthread_exit(NULL); }
		}
		if (!err)
		{ logg->print(Log::INFO, client_id, "Client left."); pthread_exit(NULL); }
		logg->print(Log::INFO, client_id, "Recieved client message.");

		if (type)
		{
			logg->print(Log::INFO, client_id, "Started image processing.");
			recv(my_socket, &in_size, sizeof(in_size), 0);
			for (; in_size; --in_size)
			{
				datsiz.push_back(DataSize(0, 0));
				recv(my_socket, &datsiz.back().first, 8, 0);
				recv(my_socket, &datsiz.back().second, 8, 0);
			}

			logg->print(Log::INFO, client_id, "Waiting for image server entry.");
			sem_wait(&sem->clsem);
			logg->print(Log::INFO, client_id, "Entered image server.");

			for (datsiz_iter = datsiz.cbegin();
				datsiz_iter != datsiz.cend() - 1;
				++datsiz_iter)
			{
				in_size = (*datsiz_iter).first;
				data = new char[in_size];
				recv(my_socket, data, in_size, 0);
				img_name = data;
				delete[] data;

				in_size = (*datsiz_iter).second;
				data = new char[in_size];
				recv(my_socket, data, in_size, 0);
				img_file.open(img_name, std::ios::binary);
				img_file.write(data, in_size);
				img_file.close();
				delete[] data;
			}

			logg->print(Log::INFO, client_id, "Passing processing to image server.");
			in_size = datsiz.back().first;
			data = new char[in_size];
			recv(my_socket, data, in_size, 0);
			sem_wait(&sem->svsem);
			memcpy(&sem->comm, data, in_size);
			sem->proc = true;
			sem_post(&sem->svsem);
			delete[] data;

			in_size = datsiz.back().second;
			data = new char[in_size];
			recv(my_socket, data, in_size, 0);
			img_name = data;
			delete[] data;

			logg->print(Log::INFO, client_id, "Waiting for image server result.");
			while (sem->proc);
			logg->print(Log::INFO, client_id, "Sending image result.");
			img_outfile.open(img_name, std::ios::binary);
			if (img_outfile.is_open())
			{
				img_outfile.seekg(0, img_outfile.end);
				in_size = img_outfile.tellg();
				img_outfile.seekg(0, img_outfile.beg);
				data = new char[in_size];
				img_outfile.read(data, in_size);
				img_outfile.close();
				send(my_socket, &in_size, sizeof(in_size), 0);
				send(my_socket, data, in_size, 0);
				delete[] data;
				logg->print(Log::INFO, client_id, "Image processing finished.");
			}
			else
			{
				logg->print(Log::ERROR, client_id, "Image server processing error.");
				in_size = 0;
				send(my_socket, &in_size, sizeof(in_size), 0);
			}
			sem_post(&sem->svsem);
			datsiz.clear();
			logg->print(Log::INFO, client_id, "Left image server.");
		}

		else
		{
			logg->print(Log::INFO, client_id, "Forwarding message to game server.");
			qmsg = { client_id, true, 0 };
			recv(my_socket, &qmsg.amount, sizeof(qmsg.amount), 0);
			msgsnd(qid[0], &qmsg, sizeof(qmsg), 0);
		}
	}

	close(my_socket);
	*my_iter = mthread;
}


void imageServer()
{
	logg->print(Log::INFO, "ImageServer", "ImageServer running.");
	while (true)
	{
		sem_wait(&sem->svsem);
		if (sem->proc)
		{
			logg->print(Log::INFO, "ImageServer", "Processing request...");
			system(sem->comm);
			sem->proc = false;
			logg->print(Log::INFO, "ImageServer", "Processing finished.");
		}
		sem_post(&sem->svsem);
		sleep(1);
	}
}


void gameServer()
{
	logg->print(Log::INFO, "GameServer", "GameServer running.");
	std::stringstream logmsg;
	bool won;
	GameSession* session;
	GameMsg cmsg;
    uint64_t waiting[2] = { 0, 0 };
	using SessionID = std::pair<uint64_t, GameSession*>;
	std::map<uint64_t, GameSession*> sessions;

	while (true)
	{
		msgrcv(qid[0], &cmsg, sizeof(GameMsg), 0, 0);
		logmsg << "Received message from " << cmsg.mtype << '.';
		logg->print(Log::INFO, "GameServer", logmsg);

		if (sessions.count(cmsg.mtype))
		{
			logmsg << cmsg.mtype << " in game.";
			logg->print(Log::INFO, "GameServer", logmsg);
			session = sessions[cmsg.mtype];

			if ((cmsg.mtype == session->clid[0]) != session->player)
			{
				logmsg << "Unexpected message from " << cmsg.mtype << '.';
				logg->print(Log::WARNING, "GameServer", logmsg);
				continue;
			}

			try
			{
				won = !session->subtract(cmsg.amount);
				logmsg << cmsg.mtype << " had a turn.";
			}
			catch (const GameSession::Errors exc)
			{
				logmsg << cmsg.mtype << " illegal turn rejected.";
				logg->print(Log::INFO, "GameServer", logmsg);

				cmsg = { cmsg.mtype, true, session->left };
				msgsnd(qid[1], &cmsg, sizeof(GameMsg), 0);
				continue;
			}

			logmsg << "Sending game data to " << session->clid[0];
			logmsg << " and " << session->clid[1] << '.';
			logg->print(Log::INFO, "GameServer", logmsg);

			cmsg = { session->clid[0], session->player, session->left };
			msgsnd(qid[1], &cmsg, sizeof(GameMsg), 0);
			cmsg = { session->clid[1], !session->player, session->left };
			msgsnd(qid[1], &cmsg, sizeof(GameMsg), 0);
			if (won)
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
			logmsg << "Starting session for " << waiting[0];
			logmsg << " and " << waiting[1];
			logg->print(Log::INFO, "GameServer", logmsg);
			waiting[1] = cmsg.mtype;
			session = new GameSession;
			session->clid[0] = waiting[0];
			session->clid[1] = waiting[1];
			sessions.insert(SessionID(waiting[0], session));
			sessions.insert(SessionID(waiting[1], session));
			cmsg = { waiting[0], true, session->left };
			msgsnd(qid[1], &cmsg, sizeof(GameMsg), 0);
			cmsg = { waiting[1], false, session->left };
			msgsnd(qid[1], &cmsg, sizeof(GameMsg), 0);
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


bool GameSession::subtract(const uint8_t _amount)
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
	return !left;
}
