#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "Errcode.h"
#include "Server.h"

using namespace std;

Server server;
void graceful_shutdown(int signo) {
	if( signo == SIGTERM || signo == SIGQUIT )
		server.interrupt();
}

int main(int argc, char** argv) {
	srand(time(NULL));
	if(argc != 2) {
		cerr << "ERROR: invalid number of arguments" << endl;
		exit(1);
	}
	signal(SIGTERM, graceful_shutdown);
	signal(SIGQUIT, graceful_shutdown);
	server.init(argv[1]);
	while(1) {
		try {
			// only call connect if handshake is successful
			if (server.handshake())
				server.connect();
		}
		catch (ERR e) {
			p_error(e);
		}
	}
}
