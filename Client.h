#ifndef CLIENT_H
#define CLIENT_H

#include <cstdint>
#include <string>
#include <vector>
#include <utility>

#include "SegHeader.h"
#include "Timer.h"

using namespace std;

const int16_t MAX_CWND = 10240;
const int16_t MIN_CWND = 512;
const int RTO = 500/*ms*/;

class Client {
private:
	int16_t last_success_acknum;
	int ack_repeat;
	bool fr_mode;
	bool file_exhausted;
	/*
	* seqnum of ACK packet from server
	*/ 
	int16_t const_seqnum;
	int socket_fd;
	struct sockaddr_in servaddr;
	vector<pair<struct header, string>> cwnd;
	int16_t cwnd_size;
	int16_t ssthresh;
	int file_fd;
	Timer timer;
	/* used for 10 s timeout */
	Timer time10;


	void send_chunk(string data, struct header h);
	void fill_cwnd_buf_and_send();
	/* when an ACK is received */
	void check_and_remove_cwnd_buf(struct header h);
	/* when a non-duplicate ACK is received, increment cwnd_size */ 
	void ss_ca_state_change();
	/* called in each iteration */
	void check_timeout_rexmit();
	/* called after timeout */
	void timeout_ss_ca_state_change();
	void enter_fast_rexmit_mode();
	void exit_fast_rexmit_mode();
	/*
	* keep running and checking for fin until 2 seconds timeout
	*/ 
	void send_fin(int16_t seqnum);
	void wait_for_fin();
	void wait_for_ack();
public:
	Client();
	~Client() { close(this->file_fd); };
	void init(string hostname, string port, int file_fd);
	/* 
	* return the expected ack number once handshake is complete
	*/	
	int handshake();
	void handshakeACK(int seqnum);
	void transmit();
	void handwave();
};

#endif
