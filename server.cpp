#include "util.h"


int qid;
SharedSem* sem;
pthread_t mthread;
std::vector<pthread_t> active;


int main()
{
	try {
		mthread = pthread_self();

		qid = msgget(QUEUE_KEY, IPC_CREAT);
		if (qid < 0)
			throw std::string("msgget() error") + std::to_string(errno);

		int shmid = shmget(SHM_KEY, sizeof(SharedSem), IPC_CREAT | 0777);
		if (shmid < 0)
			throw std::string("shmget() error") + std::to_string(errno);

		sem = (SharedSem *) shmat(shmid, NULL, 0);
		if (!sem)
			throw std::string("shmat() error") + std::to_string(errno);

		if (sem_init(&sem->clsem, 1, 1) || sem_init(&sem->svsem, 1, 1))
			throw std::string("sem_init() error") + std::to_string(errno);

		int game_pid = fork();
		if (game_pid < 0)
			throw std::string("fork() error") + std::to_string(errno);
		if (game_pid == 0)
			gameServer();

		int image_pid = fork();
		if (image_pid < 0)
			throw std::string("fork() error") + std::to_string(errno);
		if (image_pid == 0)
			imageServer();

		netServer();
		return 0;
	}
	catch (const std::string& _err)
	{
		std::cout << _err;
		return 1;
	}
}


void netServer()
{
	auto thr_iter = active.cbegin();
	int master_socket, nsocket;
	if ((master_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		throw std::string("socket() error") + std::to_string(errno);

	sockaddr_in address;
	memset(&address, 0, sizeof(sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(8888);
	uint32_t addrlen = sizeof(address);

	const uint8_t tflg = 1;
	if (setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, &tflg, 1))
		throw std::string("setsockopt() error") + std::to_string(errno);

	if (bind(master_socket, (sockaddr *) &address, sizeof(address)))
		throw std::string("bind() error") + std::to_string(errno);

	if (listen(master_socket, MAX_CONNECTIONS))
		throw std::string("listen() error") + std::to_string(errno);

	while (true)
	{
	        nsocket = accept(master_socket, (sockaddr *) &address, &addrlen);
			if (nsocket)
				throw std::string("accept() error") + std::to_string(errno);

			thr_iter = std::find(active.cbegin(), active.cend(), mthread);
			if (thr_iter == active.cend())
			{
				active.push_back(mthread);
				thr_iter = --active.cend();
			}
			if (pthread_create((pthread_t*) &(*thr_iter), NULL,
					&clientInteractor, &nsocket))
				throw std::string("pthread_create() error")
						+ std::to_string(errno);;
	}
}


void *clientInteractor(void *msock)
{
	int my_socket = *(int*) msock, sockerr;
	auto my_iter = std::find(active.begin(), active.end(), pthread_self());
	uint64_t client_id = my_iter - active.cbegin();

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
			msgrcv(qid, &qmsg, sizeof(qmsg), client_id * 2 + 1,	IPC_NOWAIT);
			if (errno == ENOMSG)
				break;

			send(my_socket, &qmsg.amount, sizeof(qmsg.amount), 0);
			send(my_socket, &qmsg.turn, sizeof(qmsg.turn), 0);
		}

		sockerr = recv(my_socket, &type, sizeof(type), MSG_DONTWAIT);
		if (sockerr < 0)
			continue;
		if (!sockerr)
			return NULL;

		if (type)
		{
			recv(my_socket, &in_size, sizeof(in_size), 0);
			for (; in_size; --in_size)
			{
				datsiz.push_back(DataSize(0, 0));
				recv(my_socket, &datsiz.back().first, 8, 0);
				recv(my_socket, &datsiz.back().second, 8, 0);
			}

			sem_wait(&sem->clsem);

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

			while (sem->proc);
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
			}
			else
			{
				in_size = 0;
				send(my_socket, &in_size, sizeof(in_size), 0);
			}
			sem_post(&sem->svsem);
			datsiz.clear();
		}

		else
		{
			qmsg = { client_id * 2, true, type };
			recv(my_socket, &qmsg.amount, sizeof(qmsg.amount), 0);
			msgsnd(qid, &qmsg, sizeof(qmsg), 0);
		}
	}

	close(my_socket);
	*my_iter = mthread;
	return NULL;
}


void imageServer()
{
	while (true)
	{
		sem_wait(&sem->svsem);
		if (sem->proc)
		{
			system(sem->comm);
			sem->proc = false;
		}
		sem_post(&sem->svsem);
	}
}


void gameServer()
{
	bool won;
	GameSession* session;
	GameMsg cmsg;
	uint64_t waiting[2];
	using SessionID = std::pair<uint64_t, GameSession*>;
	std::map<uint64_t, GameSession*> sessions;

	while (true)
	{
		msgrcv(qid, &cmsg, sizeof(GameMsg), 1, 0);

		if (sessions.count(cmsg.mtype / 2))
		{
			session = sessions[cmsg.mtype / 2];

			if ((cmsg.mtype / 2 == session->clid[0]) != session->player)
				continue;

			try { won = !session->subtract(cmsg.amount); }
			catch (const GameSession::Errors& exc)
			{
				cmsg = { cmsg.mtype * 2 + 1, true, session->left };
				msgsnd(qid, &cmsg, sizeof(GameMsg), 0);
				continue;
			}

			cmsg = { session->clid[0] * 2 + 1, session->player, session->left };
			msgsnd(qid, &cmsg, sizeof(GameMsg), 0);
			cmsg = { session->clid[1] * 2 + 1, !session->player, session->left };
			msgsnd(qid, &cmsg, sizeof(GameMsg), 0);
			if (won)
			{
				sessions.erase(sessions.find(session->clid[0]));
				sessions.erase(sessions.find(session->clid[1]));
				delete session;
			}
		}

		else if (waiting[0])
		{
			waiting[1] = cmsg.mtype / 2;
			session = new GameSession;
			session->clid[0] = waiting[0];
			session->clid[1] = waiting[1];
			sessions.insert(SessionID(waiting[0], session));
			sessions.insert(SessionID(waiting[1], session));
			cmsg = { waiting[0] * 2 + 1, true, session->left };
			msgsnd(qid, &cmsg, sizeof(GameMsg), 0);
			cmsg = { waiting[1] * 2 + 1, false, session->left };
			msgsnd(qid, &cmsg, sizeof(GameMsg), 0);
			waiting[0] = 0;
			waiting[1] = 0;
		}

		else
			waiting[0] = cmsg.mtype / 2;
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
