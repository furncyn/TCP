#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "Connection.h"
#include "Errcode.h"
#include "SegHeader.h"
#include "Server.h"
#include "Utils.h"

#define PORT    8080
#define BUF_SIZE 1024

using namespace std;

Server::Server()
	:num_conn(0), socket_fd(-1), conn(NULL){}

void Server::init(string port) {
	// create a UDP socket
	if((this->socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		p_error("socket creation failed");
		exit(1);
	}
	// filling server information
	int port_number = atoi(port.c_str());
	this->servaddr.sin_family = AF_INET;
	this->servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	this->servaddr.sin_port = htons(port_number);
	
	// bind server address to socket descriptor
	if(bind(this->socket_fd, (const struct sockaddr*) &(this->servaddr), sizeof(this->servaddr)) < 0) {
		p_error("bind failed");
		exit(1);
	}
}

bool Server::handshake() {
	char buf[12]; 
	int byteread = -1;
	// TRUTH: rely on this line to hang the server
	struct sockaddr_in cliaddr;
	socklen_t len = sizeof(cliaddr);
	byteread = recvfrom(this->socket_fd, buf, 12, 0, 
			(struct sockaddr*) &(cliaddr), &len);
	// assume the same endianess
	struct header *synhead = (struct header*) buf;
	print_recv(synhead, 0, 0);
	if(synhead->flags != F_SYN) return false;
	if(byteread != 12) {
		p_error("SYN message length incorrect");
		throw HEADER_ERR;
	}
	if (synhead->acknum != 0) {
		p_error("SYN message ACK num non-zero");
		throw HEADER_ERR;
	}
	this->num_conn++;
	string file = to_string(this->num_conn) + ".file";
	int fd = open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
	this->conn = new Connection(this->socket_fd, cliaddr, fd);
	string data = this->conn->handshake(synhead->seqnum);
	write(fd, data.c_str(), data.length());
	return true;
}

void Server::connect() {
	try {
	while(this->conn->get_is_connected()) {
		string data = this->conn->read_one_packet_and_response();
		int length = data.length();
		write(this->conn->get_fd(), data.c_str(), length);
	}
	}
	catch (ERR e) {
		if (e == TIMEOUT) {
			p_error(e);
		}
		else if (e == HEADER_ERR) {
			p_error(e);
		}
	}
	delete this->conn;
	this->conn = NULL;
}

void Server::interrupt() {
	if (this->conn) {
		int fd = this->conn->get_fd();
		char inter[] = "INTERRUPT";
		ftruncate(fd, 0);
		lseek(fd, 0, SEEK_SET);
		write(fd, inter, sizeof(inter) - 1); // no nullbyte
	}
	exit(1);
}
