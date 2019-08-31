#ifndef SERVER_H
#define SERVER_H

#include <string>

#include "Connection.h"
#include "SegHeader.h"
#include "Utils.h"
using namespace std;

class Server {
private:
	/**
	* convention: when function returns,
	* if socket_fd is set to -1, it indicates
	* errors.
	*/
	int num_conn;
	int socket_fd;
	struct sockaddr_in servaddr;
	Connection *conn;
public:
	Server();
	void init(string port);
	bool handshake();
	void connect();
	void interrupt();
};

#endif
