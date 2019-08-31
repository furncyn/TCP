#ifndef CONNECTION_H
#define CONNECTION_H

#include <cstdint>
#include <netinet/in.h>
#include <string>
#include <utility>
#include <vector>

#include "Timer.h"

using namespace std;

const int RTO = 500/*ms*/;

class Connection {
private:
	int conn_fd;
	/**
	* this should be changed within:
	* (1) handshake();
	* (2) read_rwnd();
	*/
	int16_t expect_seqnum;
	int fd;
	const int16_t const_sent_seqnum;
	struct sockaddr_in cliaddr;
	bool is_connected;
	vector<pair<int16_t, string>> rwnd;
	void sendack(int16_t acknum, bool dup);
	void sendfin();
	void listen_for_finack(void* buf);
	string read_rwnd();
	Timer time10;
	Timer timer;
	int read_timeout(int fd, void *buf, ssize_t len, ssize_t leq);
public:
	Connection(int conn_fd, struct sockaddr_in cliaddr, int fd);
	~Connection(); 
	/**
	* cliseq is a that seq number in syn packet from client
	*/
	Connection one_iter();
	string handshake(int16_t cliseq);
	void handwave(struct header *fin);
	string read_one_packet_and_response();
	void receive(char* buf, int byteread);
	bool get_is_connected() { return this->is_connected; }
	int get_fd() { return this->fd; }
};


#endif
