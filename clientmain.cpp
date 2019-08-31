#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "Client.h"

using namespace std;

int main(int argc, char** argv) {
	srand(time(NULL));
	// to be changed
	if(argc < 3) {
		cerr << "ERROR: invalid number of arguments" << endl;
		exit(1);
	}
	int fd = open(argv[3], O_RDONLY);
	if (fd < 0) {
		cerr << "ERROR: file error" << endl;
		exit(1);
	}
	Client client;
	client.init(argv[1], argv[2], fd);
	int seqnum = client.handshake();
	client.handshakeACK(seqnum);
	client.transmit();	
	client.handwave();
}
