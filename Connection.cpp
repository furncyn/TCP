
#include <cstdint>
#include <netinet/in.h>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "Connection.h"
#include "Errcode.h"
#include "SegHeader.h"
#include "Timer.h"
#include "Utils.h"

using namespace std;
const int TIME10_OUT = 10 * 1000;

Connection::Connection(int conn_fd, struct sockaddr_in cliaddr, int fd)
	:conn_fd(conn_fd), fd(fd), const_sent_seqnum(random(0, 25600)),
	cliaddr(cliaddr), is_connected(true) 
{
	if(TIME10_OUT != 10 * 1000) 
	cerr << "⚠️ ⚠️ ⚠️ ⚠️  WARN: debug timeout " << TIME10_OUT << " is used\n";
}

Connection::~Connection() {
	close(this->fd);
	char buf[12];
	int byteread = 1; 
	while(byteread <= 0)
		byteread = recv(this->conn_fd, buf, 12, MSG_DONTWAIT);
}

int Connection::read_timeout(int fd, void* buf, ssize_t len, ssize_t leq = 0) {
	this->time10.activate(TIME10_OUT);
	while(true) {
		int byteread = recv(fd, buf, len, MSG_DONTWAIT);
		if (byteread <= leq && byteread > 0) {
			if(this->time10.get_is_active()) this->time10.deactivate();
			return byteread;
		}
		if(byteread == len) {
			if(this->time10.get_is_active()) this->time10.deactivate();
			return byteread;
		}
		if (time10.timed_out())
			throw TIMEOUT;
	}
}

void Connection::sendack(int16_t acknum, bool dup = false) {
	struct header ack = {
		.seqnum = this->const_sent_seqnum,
		.acknum = acknum,
		.flags = F_ACK,
		.pad = {}
	};
	socklen_t len = sizeof(this->cliaddr);
	sendto(this->conn_fd, &ack, sizeof(ack), 0, 
		(struct sockaddr*) &(this->cliaddr), len); 
	if (dup) print_dup("SEND", &ack, 0, 0);
	else print_send(&ack, 0, 0);

}

string Connection::read_rwnd() {
	if(this->rwnd.size() <= 0) 
		return "";
	string buf = "";
	bool hasPopped = false;
	do {
		hasPopped = false;
		for(vector<int>::size_type i = 0; i < (this->rwnd).size(); i++) {
			pair<int16_t, string> pairbuf = rwnd[i];
			if(pairbuf.first == this->expect_seqnum) {
				buf += pairbuf.second;
				this->expect_seqnum = SEQadd(this->expect_seqnum, pairbuf.second.length());
				(this->rwnd).erase((this->rwnd).begin()+i);
				hasPopped = true;
				break;
			}
		}

	} while(hasPopped);
	return buf;
}

string Connection::handshake(int16_t cliseq) {
	struct header synack = {
		// we want client to increment this
		.seqnum=SEQadd(this->const_sent_seqnum, (int16_t) -1), 
		.acknum=SEQadd(cliseq, 1),
		.flags= F_SYN | F_ACK,
		.pad={} // 0 initialzed
	};
	char *synack_str = (char *) &synack;
	socklen_t len = sizeof(this->cliaddr);
	int bytesent = sendto(this->conn_fd, synack_str, sizeof(synack), 0, 
			(struct sockaddr*) &(this->cliaddr), len);
	if (bytesent != 12) {
		p_error("SYNACK not sent correctly");
		throw HEADER_ERR;
	}
	print_send(&synack, 0, 0);
	char ackbuf[524];
	int byteread = this->read_timeout(this->conn_fd, ackbuf, 524, 524);
	if (byteread < 12) {
		p_error("ACK not received correctly");
		throw HEADER_ERR;
	}
	struct header *ack = (struct header *) ackbuf;
	print_recv(ack, 0, 0);
	if (ack->flags != F_ACK) {
		p_error("ACK bit incorrect");
		throw HEADER_ERR;
	}
	if (ack->acknum != this->const_sent_seqnum) {
		p_error("ACK number incorrect");
		throw HEADER_ERR;
	}
	if (ack->seqnum != synack.acknum) {
		p_error("SEQ number incorrect");
		throw HEADER_ERR;
	}
	int length = byteread - 12;
	if (length == 0)
		return "";
	this->expect_seqnum = SEQadd(ack->seqnum, length);
	// we ack the ack only if data is not empty
	this->sendack(this->expect_seqnum);
	string data = string(ackbuf+12, length);
	return data;
}

string Connection::read_one_packet_and_response() {
	char buf[524]; 
	int byteread = this->read_timeout(this->conn_fd, buf, 524, 524);
	if (byteread < 12) return ""; // error, techniquely don't happen

	struct header* msg = (struct header*) buf;
	print_recv(msg, 0, 0);
	if(is_handwave(msg)) 
		this->handwave(msg);
	else if(is_packet(msg) && ! is_s1LTs2(msg->seqnum, this->expect_seqnum)) {
		this->receive(buf, byteread - 12);
		// set ACK number correctly with read_rwnd()
		string data = this->read_rwnd();
		this->sendack(this->expect_seqnum);
		return data;
	} 
	else { // DUP ACK
		// handshakeACK can also be repeated
		this->sendack(this->expect_seqnum, true);
	}
	return this->read_rwnd();
}

void Connection::sendfin() {
	if(this->timer.get_is_active()) this->timer.deactivate();
	this->timer.activate(RTO);
	struct header fin = {
		.seqnum=this->const_sent_seqnum, .acknum=0,
		.flags=F_FIN, .pad={}
	};
	char *fin_str = (char *) &fin;
	socklen_t len = sizeof(this->cliaddr);
	sendto(this->conn_fd, fin_str, sizeof(fin), 
		0, (struct sockaddr*) &(this->cliaddr), len);
	print_send(&fin, 0, 0);
	this->expect_seqnum = SEQadd(this->expect_seqnum, 1);
}

void Connection::listen_for_finack(void* buf) {
	struct header* recvpkt = (struct header*) buf;
	// if FIN is received, resend ACK and FIN
	// if ACK is received, close the connection
	print_recv(recvpkt, 0, 0);
	if(recvpkt->flags == F_FIN) {
       		this->sendack(SEQadd(recvpkt->seqnum, 1));
		this->sendfin();	
	}
	else if(recvpkt->flags == F_ACK) {
	       	if(this->time10.get_is_active()) this->time10.deactivate();
		this->timer.deactivate();
	       	if (recvpkt->seqnum != this->expect_seqnum) {
	       		p_error("ACK SEQ num incorrect");
	       		cerr << "expect: " << this->expect_seqnum 
	       			<< " actual: " << recvpkt->seqnum << endl;
	       		throw HEADER_ERR;
	       	}
	       	if (recvpkt->acknum != SEQadd(this->const_sent_seqnum, 1)) {
	       		p_error("ACK num incorrect");
	       		throw HEADER_ERR;
	       	}
		return;	
	}
	else if(this->timer.timed_out())
		this->sendfin();
}
void Connection::handwave(struct header *finrecv) {
	this->is_connected = false;
	if (finrecv->seqnum != this->expect_seqnum) {
		p_error("FIN SEQ num inccorect");
		throw HEADER_ERR;
	}
	if (finrecv->acknum != 0) {
		p_error("FIN ACK num incorrect");
		throw HEADER_ERR;
	}
	this->sendack(SEQadd(finrecv->seqnum, 1));
	this->sendfin();
	char buf[12];
	this->read_timeout(this->conn_fd, buf, 12);	
	this->listen_for_finack(buf);
}

// byteread is the length of content excluding the header
void Connection::receive(char* buf, int byteread) {
	struct header* packet_header = (struct header*) buf;
	if(packet_header->acknum != 0) {
		p_error("Packet ACK num inccorect");
		throw HEADER_ERR;
	}
	// Add seqnum, content pair into rwnd vector
	string content(buf+12, byteread);
	pair<int16_t, string> buffer(packet_header->seqnum, content);
	this->rwnd.push_back(buffer);
}

