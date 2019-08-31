#ifndef UTIL_H
#define UTIL_H

#include <cstdint> 
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <string>

#include "SegHeader.h"
using namespace std;

inline void p_error(string s) {
	cerr << "ERROR: ";
	perror(s.c_str());
}

/* min and max are inclusive */
inline int random(int min, int max) {
	return rand() % max + min;
}

/* off ranges from [-25600, INT_MAX] */ 
inline int16_t SEQadd(int16_t seq, int16_t off) {
	if (seq + off < 0) return seq + off + 25601;
	return (seq + off) % 25601;
}

inline void print_help(string cmd, const struct header *h, int cwnd, int ssthresh, bool dup) {
	string flags = (h->flags & F_ACK) == F_ACK ? 
			"ACK " : "";
	flags += (h->flags & F_SYN) == F_SYN ? 
			"SYN " : "";
	flags += (h->flags & F_FIN) == F_FIN ? 
			"FIN " : "";
	flags += dup? "DUP ": "";
	printf("%s %d %d %d %d %s\n", 
		cmd.c_str(), h->seqnum, h->acknum, 
		cwnd, ssthresh,
		flags.c_str());
}


inline void print_recv(const struct header *h, int cwnd, int ssthresh) {
	print_help("RECV", h, cwnd, ssthresh, false);
} 

inline void print_dup(string cmd, const struct header *h, int cwnd, int ssthresh) {
	print_help(cmd, h, cwnd, ssthresh, true);
}

inline void print_send(const struct header *h, int cwnd, int ssthresh) {
	print_help("SEND", h, cwnd, ssthresh, false);
}

inline bool is_handwave(const struct header* msg) {
	return(msg->flags == F_FIN);
}
inline bool is_packet(const struct header* msg) {
	return(msg->flags == 0);
}

/* s2 is seqnum */
inline bool is_s1LTs2(int16_t s1, int16_t s2) {
	if (s1 < s2) return true;
	if (s2 < 10240) {
		int temp = 25600 - 10240 + s2;
		return s1 > temp && s1 <= 25600;
	}
	return false;
}

#endif
