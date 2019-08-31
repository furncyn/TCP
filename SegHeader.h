#ifndef SEGHEADER_H
#define SEGHEADER_H

#include <cstdint>

const char F_ACK = 0x4;
const char F_SYN = 0x2;
const char F_FIN = 0x1;

struct header {
	int16_t seqnum; 
	int16_t acknum;
	char flags;
	char pad[7];
};

#endif
