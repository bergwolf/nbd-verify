#include <iostream>
#include <stdint.h>
#include <stdlib.h>

#include "utils-str.h"

uint64_t bytes_to_u64(const unsigned char *in)
{
	return (uint64_t(in[0]) << 56) |
		(uint64_t(in[1]) << 48) |
		(uint64_t(in[2]) << 40) |
		(uint64_t(in[3]) << 32) |
		(uint64_t(in[4]) << 24) |
		(uint64_t(in[5]) << 16) |
		(uint64_t(in[6]) <<  8) |
		uint64_t(in[7]);
}

void u64_to_bytes(unsigned char *p, uint64_t what)
{
	for(int index=0; index<8; index++)
		p[index] = (what << index * 8) >> 56;
}

uint32_t bytes_to_u32(const unsigned char *in)
{
	return (in[0] << 24) |
		(in[1] << 16) |
		(in[2] <<  8) |
		in[3];
}

void u32_to_bytes(unsigned char *p, uint32_t what)
{
	for(int index=0; index<4; index++)
		p[index] = (what << index * 8) >> 24;
}

void hex_dump(const unsigned char *in, int size)
{
	for(int index=0; index<size; index++)
		std::cout << format("%02x ", in[index]);
}

// not very random
// in fact: this makes it repeatable (do not change the
// seeding!) so that problems can more easily be 
// repeated
void get_random_bytes(unsigned char *p, int len)
{
	static bool initted = false;

	if (!initted)
	{
		srand48(1);
		initted = true;
	}

	for(int index=0; index<len; index++)
		p[index] = lrand48();
}

uint64_t get_random_block_offset(uint64_t n_blocks)
{
	uint64_t dummy = uint64_t(lrand48()) << 32;

	dummy |= lrand48();

	return dummy % n_blocks;
}
