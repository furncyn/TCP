#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <chrono>
#include <unistd.h>

#include "Client.h"
#include "SegHeader.h"
#include "Timer.h"
#include "Utils.h"

using namespace std;
using namespace std::chrono;

const int TIME10_OUT = 10 * 1000;

Client::Client()
	:ack_repeat(0), fr_mode(false),
	file_exhausted(false), cwnd_size(512), ssthresh(5120){};

void Client::init(string hostname, string port, int file_fd) {
	if(TIME10_OUT != 10 * 1000) 
		cerr << "⚠️ ⚠️ ⚠️ ⚠️  WARN: debug timeout "<< TIME10_OUT <<" is used\n";
	this->file_fd = file_fd;
	// create a UDP socket
	if((this->socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		p_error("socket creation failed");
		exit(1);
	}
	// resolving hostname
	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = 0;
	hints.ai_protocol = 0;

	struct addrinfo *result, *rp;
	if(getaddrinfo(hostname.c_str(), port.c_str(), &hints, &result) != 0) {
		p_error("getaddrinfo failed");
		exit(1);	
	} 
	for(rp = result; rp != NULL; rp = rp->ai_next) {
		this->socket_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if(this->socket_fd == -1)
			continue;
		if(connect(this->socket_fd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;
		close(this->socket_fd);
	}
	if(rp == NULL) {
		p_error("cannot connect");
		exit(1);
	}
	freeaddrinfo(result);
}

int Client::handshake() {
	struct header synmsg;
	int seqnum = random(0, 25600);
	synmsg.seqnum = seqnum;
	synmsg.acknum = 0;
	synmsg.flags = F_SYN;
	int bytesend = send(this->socket_fd, &synmsg, sizeof(synmsg), 0);
	if (bytesend != 12) {
		close(this->socket_fd);
		p_error("Cannot send SYN message");
		exit(1);
	}
	print_send(&synmsg, this->cwnd_size, this->ssthresh);
	char buf[12];

	if (! this->time10.get_is_active()) this->time10.activate(TIME10_OUT);

	int byteread;
	while(true) {
		byteread = recv(this->socket_fd, buf, 12, MSG_DONTWAIT);
		if (byteread == 12) {
			this->time10.deactivate();
			break;
		} else if (this->time10.timed_out()) {
			close(this->socket_fd);
			p_error("Timeout on receive SYNACK");
			exit(1);
		}
	}

	struct header* synack = (struct header*) buf;
	print_recv(synack, this->cwnd_size, this->ssthresh);
	if (synack->flags != (F_ACK | F_SYN)) {
		close(this->socket_fd);
		p_error("SYNACK bit incorrect");
		exit(1);
	}
	if(synack->acknum != SEQadd(seqnum, 1)) {
		close(this->socket_fd);
		p_error("ACK num incorrect");
		exit(1);
	}
	this->const_seqnum = SEQadd(synack->seqnum, 1);
	return SEQadd(seqnum, 1);
}

void Client::send_chunk(string data, struct header h) {
	string buf = string((char *)&h, sizeof(h));
	buf.append(data);
	send(this->socket_fd, buf.c_str(), buf.length(), 0);
	// print_send(&h, this->cwnd_size, this->ssthresh);
}

void Client::handshakeACK(int seqnum) {
	struct header h;
	h.seqnum = seqnum;
	h.acknum = this->const_seqnum;
	h.flags = F_ACK;
	char buf[512];
	int byteread = read(this->file_fd, buf, 512);
	string data = string(buf, byteread);
	// this->expect_acknum = SEQadd(seqnum, data.length());
	this->send_chunk(data, h);
	// add to cwnd
	this->cwnd.push_back(make_pair(h, data));
	// start the timer
	timer.activate(RTO);
	print_send(&h, this->cwnd_size, this->ssthresh);
}

void Client::send_fin(int16_t seqnum) {
	struct header finsend = {
		.seqnum=seqnum, .acknum=0, .flags = F_FIN, .pad={}
	};
	send(this->socket_fd, &finsend, sizeof(finsend), 0);
	print_send(&finsend, this->cwnd_size, this->ssthresh);
	// start RTO timer
	if(! this->timer.get_is_active()) this->timer.activate(RTO);
}

void Client::wait_for_ack() {
	while(! this->file_exhausted || this->cwnd.size() != 0) {
		char buf[12];
		int byteread = -1;
		byteread = recv(this->socket_fd, buf, 12, MSG_DONTWAIT); 	
		struct header* h = (struct header*) buf;
		if(byteread == 12 && h->flags == F_ACK 
			&& h->acknum == SEQadd(this->last_success_acknum, 1) 
			&& h->seqnum == this->const_seqnum) {
				if(this->timer.get_is_active()) this->timer.deactivate();
				if(this->time10.get_is_active()) this->time10.deactivate();
				print_recv(h, this->cwnd_size, this->ssthresh);
		 		// assume that there is only one element in cwnd.size
				(this->cwnd).pop_back();
		}
		else if(this->timer.timed_out()) {
			// retransmit FIN packet
			if(this->timer.get_is_active()) this->timer.deactivate();
			auto itr = (this->cwnd).begin();
			this->send_fin(itr->first.seqnum);
		}
		else if(this->time10.timed_out()) {
			close(this->socket_fd);
			p_error("server response 10s timeout");
			exit(1); 
		}
	}
}

void Client::wait_for_fin() {
	char buf[12];
	int byteread = -1;
	byteread = recv(this->socket_fd, buf, 12, MSG_DONTWAIT); 	
	struct header* finrecv = (struct header*) buf;                  	
	if (finrecv->flags == F_FIN) {
		if(byteread != 12) {
			close(this->socket_fd);
			perror("Error on receiving FIN");
		} 
		if (finrecv->acknum != 0) {
			close(this->socket_fd);
			p_error("ACK num incorrect");
		}
		if (finrecv->seqnum != this->const_seqnum) {
			close(this->socket_fd);
			p_error("SEQ num incorrect");
		}
		print_recv(finrecv, this->cwnd_size, this->ssthresh);
		struct header acksend;
		acksend.seqnum = SEQadd(this->last_success_acknum, 1);
		acksend.acknum = SEQadd(finrecv->seqnum, 1); 
		acksend.flags = F_ACK;
	        this->send_chunk("", acksend);
		print_send(&acksend, this->cwnd_size, this->ssthresh);
	}
}

void Client::handwave() {
	struct header finsend = {
		.seqnum = this->last_success_acknum, .acknum = 0,
		.flags = F_FIN, .pad={}
	};	
	// put into cwnd
	string data = "";
	(this->cwnd).push_back(make_pair(finsend, data));
	// transmit
	send_fin(this->last_success_acknum);
	// start 10s timer
	if (! this->time10.get_is_active()) this->time10.activate(TIME10_OUT);
	// wait for correct ACK
	this->wait_for_ack();
	/* Wait for 2 seconds and drop any non-FIN packet */
	high_resolution_clock::time_point start_t = high_resolution_clock::now();
	auto time_elapsed = 0;
	while(time_elapsed <= 2) {
		time_elapsed = duration_cast<seconds>( high_resolution_clock::now() - start_t).count();
		this->wait_for_fin();
	}
	close(this->socket_fd);
}

void Client::fill_cwnd_buf_and_send() {
	int total_buf_size = 0;
	/* get cwnd length */
	size_t len = this->cwnd.size();
	for (size_t i = 0; i < len; i++) {
		total_buf_size += this->cwnd[i].second.length();
	}
	/* fill cwnd */
	while(cwnd_size - total_buf_size >= 512) {
		char buf[512];
		int byteread = read(this->file_fd, buf, 512);
		// this means there is no more data
		if (byteread == 0) {
			this->file_exhausted = true;
			return;
		}
		// get data
		string data = string(buf, byteread);
		// form header
		len = this->cwnd.size(); // can change each iteration
		int16_t seqnum;
		if (len == 0) seqnum = this->last_success_acknum;
		else seqnum = SEQadd(this->cwnd[len - 1].first.seqnum,
				this->cwnd[len-1].second.length());
		struct header h = {
			.seqnum=seqnum, .acknum=0, .flags=0, .pad={}
		};
		// put into cwnd
		(this->cwnd).push_back(make_pair(h, data));
		// transmit
		this->send_chunk(data, h);
		print_send(&h, this->cwnd_size, this->ssthresh);
		// change for potential next packet
		total_buf_size += byteread;
	}
}

void Client::check_and_remove_cwnd_buf(struct header h) {
	int acknum = h.acknum;
	auto itr = (this->cwnd).begin();

	while(itr != this->cwnd.end()) {
		// ack for this packet
		int16_t p_ack = SEQadd(itr->first.seqnum, itr->second.length());
		if (acknum == p_ack) {
			this->cwnd.erase(this->cwnd.begin(), itr + 1);
			this->last_success_acknum = p_ack;
			this->ack_repeat = 0;
			if (this->timer.get_is_active())
				this->timer.deactivate();
			break;
		}
		itr++;
	}
}

void Client::check_timeout_rexmit() {
	auto itr = (this->cwnd).begin();
	this->send_chunk(itr->second, itr->first);
	print_send(&(itr->first), this->cwnd_size, this->ssthresh);
}

void Client::transmit() {
	while(! this->file_exhausted || this->cwnd.size() != 0) {
		fill_cwnd_buf_and_send();
		
		char buf[12];
		// transmission is complete
		if (this->cwnd.size() == 0 && this->file_exhausted) break;

		// activate timer if needed
		if (this->cwnd.size() != 0 && ! timer.get_is_active()) 
			timer.activate(RTO);

		if (! this->time10.get_is_active()) this->time10.activate(TIME10_OUT);

		int byteread = recv(this->socket_fd, buf, 12, MSG_DONTWAIT);
		if (byteread == 12)  {
			struct header* h = (struct header *) buf;
			if (this->time10.get_is_active()) this->time10.deactivate();

			int16_t acknum = h->acknum;
			print_recv(h, this->cwnd_size, this->ssthresh);
			if (acknum != this->last_success_acknum) {
				if (this->fr_mode) 
					this->exit_fast_rexmit_mode();
				this->ss_ca_state_change();
				// receive ACK clear cwnd
				this->check_and_remove_cwnd_buf(*h);
			}
			else {
				this->ack_repeat++;
				if (this->ack_repeat == 3) {
					this->enter_fast_rexmit_mode();
				} 
				else if (this->ack_repeat > 3){
					this->cwnd_size += 512;
					if (this->cwnd_size > 10240) 
						this->cwnd_size = 10240;
				}	
			}
			// receive ACK increment cwnd
		}
		else if (this->time10.timed_out()) {
			p_error("server response 10s timeout");
			exit(1);
		}
		if(this->timer.get_is_active() && this->timer.timed_out()) {
			this->check_timeout_rexmit();
			// restart timer
			if (timer.get_is_active()) timer.deactivate();	
			timer.activate(RTO);
		}

	}
	// if(this->file_exhausted && (this->cwnd).size() == 0) {
	//this->handwave();
	//}
}

void Client::ss_ca_state_change() {
	// it is fine to use normal addition here.
	if (this->cwnd_size < this->ssthresh) 
		this->cwnd_size += 512;
	else 
		this->cwnd_size += (512 * 512 / this->cwnd_size);
	
	if (this->cwnd_size > MAX_CWND) 
		this->cwnd_size = MAX_CWND;
}


void Client::timeout_ss_ca_state_change() {
	int16_t half_cwnd = this->cwnd_size / 2;
	this->ssthresh = half_cwnd > 1024 ? half_cwnd : 1024;
}

void Client::enter_fast_rexmit_mode() {
	this->fr_mode = true;
	int16_t half_cwnd = this->cwnd_size / 2;
	// cerr << "fast rexmit enter" << endl;
	this->ssthresh = half_cwnd > 1024 ? half_cwnd : 1024;
	this->cwnd_size = this->ssthresh + 1536;

	if (this->cwnd.empty()) {
		p_error("cwnd empty cannot rexmit");
		return;
	}
	struct header h = this->cwnd[0].first;
	string data = this->cwnd[0].second;
	this->send_chunk(data, h);
	print_dup("SEND", &h, this->cwnd_size, this->ssthresh);
}

void Client::exit_fast_rexmit_mode() {
	// cerr << "fast rexmit leave" << endl;
	// cerr << "cwnd: " << this->cwnd_size 
	// 	<< "ssthresh: " << this->ssthresh << endl;
	this->fr_mode = false;
	this->cwnd_size = this->ssthresh;
}

