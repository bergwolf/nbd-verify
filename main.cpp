#include <assert.h>
#include <iostream>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nbd.h"
#include "utils-data.h"
#include "utils-net.h"
#include "utils-str.h"
#include "utils-time.h"

#define BLOCK_SIZE 4096

#define N_RANDOM_BLOCKS 2501

#define LATENCY_MEASURE_TIME 5.0

#define NOOP_TEST_STEPS 4

#define DATA_CHECK_N_BLOCKS 256

typedef enum { A_VERIFY, A_IOPS, A_LATENCY } action_t;

int verify_device_has_no_data(const char *host, int port)
{
	uint32_t flags = -1;
	uint64_t size = -1;
	int fd = connect_nbd(host, port, &size, &flags, false);
	if (fd == -1)
	{
		std::cerr << "failed setting up NBD session" << std::endl;
		return -1;
	}

	uint64_t n_bytes = DATA_CHECK_N_BLOCKS * BLOCK_SIZE;

	if (size < n_bytes)
	{
		std::cerr << "Device is too small" << std::endl;
		close(fd);
		return -1;
	}

	unsigned char *data = (unsigned char *)malloc(n_bytes);

	int rc = read_nbd(fd, 0, (char *)data, n_bytes);
	if (rc)
	{
		std::cerr << "Failed reading from NBD device (offset 0, " << n_bytes << " bytes)" << std::endl;
		free(data);
		close(fd);
		return rc;
	}

	if (close_nbd(fd))
	{
		std::cerr << "Problem ending session with NBD server" << std::endl;
	}

	for(int index=0; index<n_bytes; index++)
	{
		if (data[index])
		{
			std::cerr << "Device contains data!" << std::endl;
			return -1;
		}
	}

	free(data);

	return 0;
}

void create_data_block_simple(unsigned char *p, size_t len, uint64_t value)
{
	assert(len == BLOCK_SIZE);

	memset(p, 0x00, len);

	for(int index=0; index<BLOCK_SIZE; index += sizeof(uint64_t))
		u64_to_bytes(&p[index], value);
}

int write_block(int fd, uint64_t nr, const unsigned char *data, size_t len)
{
	assert(len == BLOCK_SIZE);

	uint64_t offset = nr * BLOCK_SIZE;

	int rc = write_nbd(fd, offset, (const char *)data, len);
	if (rc)
	{
		std::cerr << "Failed writing to offset " << offset << " length " << len << ": " << rc << std::endl;
		return rc;
	}

	return 0;
}

int verify_block(int fd, uint64_t nr, const unsigned char *data, size_t len)
{
	uint64_t offset = nr * BLOCK_SIZE;

	unsigned char *in = (unsigned char *)malloc(len);

	int rc = read_nbd(fd, offset, (char *)in, len);
	if (rc)
	{
		std::cerr << "Failed reading from offset " << offset << " length " << len << ": " << rc << std::endl;
		free(in);
		return rc;
	}

	if (memcmp(in, data, len))
	{
		std::cerr << "Data mismatch while verifying block " << nr << std::endl;
		free(in);
		return -1;
	}

	free(in);

	return 0;
}

int reconnect(int *fd, std::string host, int port, uint64_t prev_size, uint32_t prev_flags, double sleep_duration)
{
	std::cout << "closing & reconnecting to " << host << " " << port << std::endl;

	if (close_nbd(*fd))
	{
		std::cerr << "Failed to close session with server" << std::endl;
		return -1;
	}

	USLEEP(useconds_t(sleep_duration * 1000000.0));

	uint32_t flags = -1;
	uint64_t size = -1;
	*fd = connect_nbd(host, port, &size, &flags, false);
	if (*fd == -1)
	{
		std::cerr << "failed setting up NBD session" << std::endl;
		return -1;
	}

	if (size != prev_size)
	{
		std::cerr << "size of device went from " << prev_size << " to " << size << std::endl;
		return -1;
	}

	if (flags != prev_flags)
	{
		std::cerr << "flags of device went from " << format("%04x", prev_flags) << " to " << format("%04x", flags) << std::endl;
		return -1;
	}

	return 0;
}

void choose_blocks(unsigned char ***random_blocks, uint64_t **nrs, uint64_t n_blocks)
{
	*random_blocks = (unsigned char **)malloc(sizeof(unsigned char *) * N_RANDOM_BLOCKS);
	*nrs = (uint64_t *)malloc(sizeof(uint64_t) * N_RANDOM_BLOCKS);

	for(int index=0; index<N_RANDOM_BLOCKS; index++)
	{
		(*random_blocks)[index] = (unsigned char *)malloc(BLOCK_SIZE);

		bool ok;
		do
		{
			(*nrs)[index] = get_random_block_offset(n_blocks);

			ok = true;

			for(int verify=0; verify<index; verify++)
			{
				if ((*nrs)[verify] == (*nrs)[index])
				{
					ok = false;
					break;
				}
			}
		}
		while(!ok);
	}
}

int nbd_verify(std::string host, int port, double sleep_duration, bool do_reconnect)
{
	uint32_t flags = -1;
	uint64_t size = -1;
	int fd = connect_nbd(host, port, &size, &flags, true);
	if (fd == -1)
	{
		std::cerr << "failed setting up NBD session" << std::endl;
		return 1;
	}

	if (size % BLOCK_SIZE)
	{
		std::cerr << "block device not multiple of " << BLOCK_SIZE << std::endl;
		close(fd);
		return 1;
	}

	uint64_t b31 = uint64_t(1) << 31;
	uint64_t b32 = uint64_t(1) << 32;

	if (size < b32 + BLOCK_SIZE * 2)
	{
		std::cerr << "device too small, must be at least 5GB" << std::endl;
		close(fd);
		return 1;
	}

	uint64_t n_blocks = size / BLOCK_SIZE;
	std::cout << std::endl << " * TEST0001: verify that data is still there after a reconnect, also verify that the server has no issues with wrapping around at 2/4GB offsets" << std::endl;

	unsigned char block_0[BLOCK_SIZE];
	create_data_block_simple(block_0, sizeof block_0, 0x1234567800000000ll);
	if (write_block(fd, 0, block_0, sizeof block_0))
		return 1;

	unsigned char block_1[BLOCK_SIZE];
	create_data_block_simple(block_1, sizeof block_0, 0x1234567800000001ll);
	if (write_block(fd, 1, block_1, sizeof block_1))
		return 1;

	unsigned char block_n[BLOCK_SIZE];
	create_data_block_simple(block_n, sizeof block_n, 0x1234567800000000ll | (size / BLOCK_SIZE));
	if (write_block(fd, n_blocks - 1, block_n, sizeof block_n))
		return 1;

	unsigned char block_2gb_min_1[BLOCK_SIZE];
	uint64_t block_nr_2gb_min_1 = b31 / BLOCK_SIZE - BLOCK_SIZE;
	create_data_block_simple(block_2gb_min_1, sizeof block_2gb_min_1, 0x1234567800000000ll | block_nr_2gb_min_1);
	if (write_block(fd, block_nr_2gb_min_1, block_2gb_min_1, sizeof block_2gb_min_1))
		return 1;

	unsigned char block_2gb[BLOCK_SIZE];
	uint64_t block_nr_2gb = b31 / BLOCK_SIZE - BLOCK_SIZE;
	create_data_block_simple(block_2gb, sizeof block_2gb, 0x1234567800000000ll | block_nr_2gb);
	if (write_block(fd, block_nr_2gb, block_2gb, sizeof block_2gb))
		return 1;

	unsigned char block_4gb_min_1[BLOCK_SIZE];
	uint64_t block_nr_4gb_min_1 = b32 / BLOCK_SIZE - BLOCK_SIZE;
	create_data_block_simple(block_4gb_min_1, sizeof block_4gb_min_1, 0x1234567800000000ll | block_nr_4gb_min_1);
	if (write_block(fd, block_nr_4gb_min_1, block_4gb_min_1, sizeof block_4gb_min_1))
		return 1;

	unsigned char block_4gb[BLOCK_SIZE];
	uint64_t block_nr_4gb = b32 / BLOCK_SIZE - BLOCK_SIZE;
	create_data_block_simple(block_4gb, sizeof block_4gb, 0x1234567800000000ll | block_nr_4gb);
	if (write_block(fd, block_nr_4gb, block_4gb, sizeof block_4gb))
		return 1;

	// reconnect to make sure the server flushed to disk etc
	if (do_reconnect)
	{
		if (reconnect(&fd, host, port, size, flags, sleep_duration))
			return 1;
	}

	if (verify_block(fd, 0, block_0, sizeof block_0))
		return 1;

	if (verify_block(fd, 1, block_1, sizeof block_1))
		return 1;

	if (verify_block(fd, n_blocks - 1, block_n, sizeof block_n))
		return 1;

	if (verify_block(fd, block_nr_2gb_min_1, block_2gb_min_1, sizeof block_2gb_min_1))
		return 1;

	if (verify_block(fd, block_nr_2gb, block_2gb, sizeof block_2gb))
		return 1;

	if (verify_block(fd, block_nr_4gb_min_1, block_4gb_min_1, sizeof block_4gb_min_1))
		return 1;

	if (verify_block(fd, block_nr_4gb, block_4gb, sizeof block_4gb))
		return 1;

	std::cout << std::endl << " * TEST0002 fill " << N_RANDOM_BLOCKS << " with random data at random locations and verify that the data is still there after a reconnect..." << std::endl;

	unsigned char **random_blocks = NULL;
	uint64_t *nrs = NULL;

	choose_blocks(&random_blocks, &nrs, n_blocks);

	for(int index=0; index<N_RANDOM_BLOCKS; index++)
	{
		create_data_block_simple(random_blocks[index], BLOCK_SIZE, 0x1234567800000000ll | nrs[index]);

		if (write_block(fd, nrs[index], random_blocks[index], BLOCK_SIZE))
			return 1;

		if (index % 7 == 0)
		{
			if (flush_nbd(fd))
			{
				std::cerr << "flush failed" << std::endl;
				return 1;
			}
		}
	}

	if (do_reconnect)
	{
		if (reconnect(&fd, host, port, size, flags, sleep_duration))
			return 1;
	}

	for(int index=0; index<N_RANDOM_BLOCKS; index++)
	{
		if (verify_block(fd, nrs[index], random_blocks[index], BLOCK_SIZE))
			return 1;
	}

	std::cout << std::endl << " * TEST0003 fill " << N_RANDOM_BLOCKS << " with the same data (de-duplication test) at random locations and verify that the data is still there after a reconnect..." << std::endl;

	unsigned char **random_blocks_dd = NULL;
	uint64_t *nrs_dd = NULL;

	choose_blocks(&random_blocks_dd, &nrs_dd, n_blocks);

	double start_ts = get_ts();
	for(int index=0; index<N_RANDOM_BLOCKS; index++)
	{
		create_data_block_simple(random_blocks_dd[index], BLOCK_SIZE, 0x87654321deadbeefll);

		if (write_block(fd, nrs_dd[index], random_blocks_dd[index], BLOCK_SIZE))
			return 1;

		if (index % 7 == 0)
		{
			if (flush_nbd(fd))
			{
				std::cerr << "flush failed" << std::endl;
				return 1;
			}
		}
	}

	double blocks_per_second = double(N_RANDOM_BLOCKS) / (get_ts() - start_ts);

	if (do_reconnect)
	{
		if (reconnect(&fd, host, port, size, flags, sleep_duration))
			return 1;
	}

	for(int index=0; index<N_RANDOM_BLOCKS; index++)
	{
		if (verify_block(fd, nrs_dd[index], random_blocks_dd[index], BLOCK_SIZE))
			return 1;
	}

	std::cout << std::endl << " * TEST0004 overwrite " << N_RANDOM_BLOCKS << " with the same data (de-duplication test) at random locations and verify that the data is still there after a reconnect..." << std::endl;

	for(int index=0; index<N_RANDOM_BLOCKS; index++)
	{
		create_data_block_simple(random_blocks[index], BLOCK_SIZE, 0x1111111122222222ll);

		if (write_block(fd, nrs[index], random_blocks[index], BLOCK_SIZE))
			return 1;

		if (index % 7 == 0)
		{
			if (flush_nbd(fd))
			{
				std::cerr << "flush failed" << std::endl;
				return 1;
			}
		}
	}

	if (do_reconnect)
	{
		if (reconnect(&fd, host, port, size, flags, sleep_duration))
			return 1;
	}

	for(int index=0; index<N_RANDOM_BLOCKS; index++)
	{
		if (verify_block(fd, nrs[index], random_blocks[index], BLOCK_SIZE))
			return 1;
	}

	std::cout << std::endl << " * TEST0005 write to an offset (and size) which is not at a multiple of the blocksize" << std::endl; 

	unsigned char two_blocks[BLOCK_SIZE * 2];
	memset(two_blocks, 0x12, BLOCK_SIZE * 2);

	uint64_t halfway_nr = 9;
	uint64_t halfway_offset = BLOCK_SIZE * halfway_nr;
	int rc = write_nbd(fd, halfway_offset, (const char *)two_blocks, BLOCK_SIZE * 2);
	if (rc)
	{
		std::cerr << "Failed writing to block " << halfway_nr << " length " << BLOCK_SIZE * 2 << ": " << rc << std::endl;
		return rc;
	}

	two_blocks[BLOCK_SIZE - 2] = two_blocks[BLOCK_SIZE - 1] =
	two_blocks[BLOCK_SIZE + 0] = two_blocks[BLOCK_SIZE + 1] = 0xa9;
	rc = write_nbd(fd, halfway_offset + BLOCK_SIZE - 2, (const char *)&two_blocks[BLOCK_SIZE - 2], 4);
	if (rc)
	{
		std::cerr << "Failed writing to block " << halfway_nr << " length 4: " << rc << std::endl;
		return rc;
	}

	if (do_reconnect)
	{
		if (reconnect(&fd, host, port, size, flags, sleep_duration))
			return 1;
	}

	if (verify_block(fd, halfway_nr, two_blocks, BLOCK_SIZE * 2))
		return 1;

	if ((flags & 32) == 0)
		std::cout << std::endl << " - TEST0006 skipping discard test: server indicates that it does not support TRIM" << std::endl;
	else
	{
		std::cout << std::endl << " * TEST0006 reset complete datastore to 0x00 (DISCARD) & verify" << std::endl;

		rc = -1;
		uint64_t dummy_size = size, dummy_offset = 0;
		while(dummy_size > 0)
		{
			uint32_t cur_size = dummy_size;

			if (dummy_size > 2147479552)
				cur_size = 2147479552;

			if ((rc = discard_nbd(fd, dummy_offset, cur_size)))
			{
				std::cerr << "discard at offset " << dummy_offset << " of " << cur_size << " bytes failed: " << rc << std::endl;
				return 1;
			}

			dummy_offset += cur_size;
			dummy_size -= cur_size;
		}

		if (do_reconnect)
		{
			if (reconnect(&fd, host, port, size, flags, sleep_duration))
				return 1;
		}

		std::cout << " verifying discard..." << std::endl;
		unsigned char zero_block[BLOCK_SIZE] = { 0 };

		for(uint64_t index=0; index<N_RANDOM_BLOCKS; index++)
		{
			if (verify_block(fd, index, zero_block, BLOCK_SIZE))
				return 1;
		}
	}

	std::cout << std::endl << " * TEST0007 verify that the nbd-server also sends a response header for a 0-bytes read request..." << std::endl;
	unsigned char buffer[BLOCK_SIZE];

	rc = read_nbd(fd, 0, (char *)buffer, 0);
	if (rc)
	{
		std::cerr << "Failed to read from server " << rc << std::endl;
		return -1;
	}

	std::cout << std::endl << " * TEST0008 verify that the nbd-server also sends a response header for a 0-bytes write request..." << std::endl;

	rc = write_nbd(fd, 0, (char *)buffer, 0);
	if (rc)
	{
		std::cerr << "Failed to write to server " << rc << std::endl;
		return -1;
	}

	std::cout << std::endl << " * TEST0009 verify that negative offsets are rejected" << std::endl;

	unsigned char offset_buffer[BLOCK_SIZE];
	uint64_t handle = 1;
	rc = send_command_nbd(fd, 1, handle, uint64_t(-BLOCK_SIZE / 2), BLOCK_SIZE);
	if (rc)
	{
		std::cerr << "Problem sending command to server!" << rc << std::endl;
		return -1;
	}

	rc = verify_ack(fd, handle);
	if (rc == 0)
	{
		std::cerr << "Server did not reject negative offset!" << rc << std::endl;
		return -1;
	}

	if (do_reconnect)
	{
		if (reconnect(&fd, host, port, size, flags, sleep_duration))
			return 1;
	}

	std::cout << std::endl << " * TEST0010 verify that writing past the device end is rejected" << std::endl;

	rc = send_command_nbd(fd, 1, ++handle, size - (BLOCK_SIZE / 2), BLOCK_SIZE);
	if (rc)
	{
		std::cerr << "Problem sending command to server!" << rc << std::endl;
		return -1;
	}

	rc = verify_ack(fd, handle);
	if (rc == 0)
	{
		std::cerr << "Server did not reject writing past device end!" << rc << std::endl;
		return -1;
	}

	if (do_reconnect)
	{
		if (reconnect(&fd, host, port, size, flags, sleep_duration))
			return 1;
	}

	// finished.

	if (close_nbd(fd))
	{
		std::cerr << "Failed to close session with server" << std::endl;
		return 1;
	}

	printf("\n ***** all fine! *****\n");

	return 0;
}

int nbd_latency(const char *host, int port, bool do_writes)
{
	uint32_t flags = -1;
	uint64_t size = -1;
	int fd = connect_nbd(host, port, &size, &flags, true);
	if (fd == -1)
	{
		std::cerr << "failed setting up NBD session" << std::endl;
		return 1;
	}

	double start_ts = get_ts(), now_ts = -1.0;
	int count = 0;

	std::cout << "Please wait " << LATENCY_MEASURE_TIME << " seconds..." << std::endl;

	if (do_writes)
		std::cerr << "measuring latency for WRITE actions" << std::endl;
	else
		std::cerr << "measuring latency for read actions" << std::endl;

	unsigned char buffer[BLOCK_SIZE];

	do
	{
		int rc = -1;

		if (do_writes)
			rc = write_nbd(fd, 0, (char *)buffer, 0);
		else
			rc = read_nbd(fd, 0, (char *)buffer, 0);

		if (rc)
		{
			std::cerr << "Failed to " << (do_writes ? "write to" : "read from") << " server " << rc << std::endl;
			return -1;
		}

		count++;

		now_ts = get_ts();
	} while(now_ts - start_ts < LATENCY_MEASURE_TIME);

	std::cout << "Latency is " << (now_ts - start_ts) * 1000.0 / double(count) << "ms" << std::endl;

	if (close_nbd(fd))
	{
		std::cerr << "Failed to close session with server" << std::endl;
		return -1;
	}

	return 0;
}

int nbd_iops(const char *host, int port, double dd_perc, bool do_writes)
{
	uint32_t flags = -1;
	uint64_t size = -1;
	int fd = connect_nbd(host, port, &size, &flags, true);
	if (fd == -1)
	{
		std::cerr << "failed setting up NBD session" << std::endl;
		return 1;
	}

	if (size < BLOCK_SIZE)
	{
		std::cerr << "device too small (" << size << "), must be at least " << BLOCK_SIZE << std::endl;
		return 1;
	}

	uint64_t n_blocks = size / BLOCK_SIZE;

	std::cout << "press ctrl+c to abort" << std::endl;

	unsigned char block_dd[BLOCK_SIZE];
	memset(block_dd, 0xfe, sizeof block_dd);

	unsigned char block_ndd[BLOCK_SIZE];

	double start_ts = get_ts(), prev_ts = start_ts;

	uint64_t nr = 0;

	if (do_writes)
		std::cerr << "measuring IOPS for WRITE actions" << std::endl;
	else
		std::cerr << "measuring IOPS for read actions" << std::endl;

	for(;;)
	{
		unsigned char *p = NULL;

		double d = drand48() * 100.0;
		if (d < dd_perc)
			p = block_dd;
		else
		{
			*(uint64_t *)block_ndd = nr;

			p = block_ndd;
		}

		uint64_t b_nr = get_random_block_offset(n_blocks);

		int rc = -1;

		if (do_writes)
			rc = write_nbd(fd, b_nr * BLOCK_SIZE, (const char *)p, BLOCK_SIZE);
		else
			rc = read_nbd(fd, b_nr * BLOCK_SIZE, (char *)p, BLOCK_SIZE);

		if (rc)
		{
			std::cerr << "Failed to " << (do_writes ? "write to" : "read from") << " server " << rc << std::endl;
			return rc;
		}

		nr++;

		double now_ts = get_ts();
		if (now_ts - prev_ts >= 2.0)
		{
			double diff_ts = now_ts - start_ts;
			printf("IOPs: %f\r", double(nr) / diff_ts);
			fflush(NULL);

			prev_ts = now_ts;
		}
	}

	return 0;
}

void help()
{
	std::cerr << "-H x     host to connect to" << std::endl;
	std::cerr << "-P x     port to connect to" << std::endl;
	std::cerr << "-a x     action, must be either \"iops\", \"latency\" or \"verify\"" << std::endl;
	std::cerr << "-p x     for iops: de-duplication percentage (how much will be de-dupable)" << std::endl;
	std::cerr << "-r       for iops/latency: do reads instead of writes (writes are the default! be warned!)" << std::endl;
	std::cerr << "-i x     how long to sleep during a disconnect/connect cycle" << std::endl;
	std::cerr << "-f       ignore check which verifies that the device does not contain data (this is a safety check)" << std::endl;
	std::cerr << "-t x     time-out for networking and command processing (" << read_timeout << ")" << std::endl;
	std::cerr << "-n       do not disconnect/connect" << std::endl;
}

int main(int argc, char *argv[])
{
	const char *host = NULL;
	int port = -1;
	action_t action = A_VERIFY;
	double dd_perc = 7.0;
	double sleep_duration = 1.1;
	bool do_reconnect = true;
	bool do_writes = true;
	bool ignore_has_data = false;

	std::cout << "nbd_verify v" VERSION ", (C) 2013 by folkert@vanheusden.com" << std::endl << std::endl;

	int c = -1;
	while((c = getopt(argc, argv, "H:P:a:p:i:nrft:")) != -1)
	{
		switch(c)
		{
			case 'H':
				host = optarg;
				break;

			case 'P':
				port = atoi(optarg);
				if (port <= 0)
				{
					std::cerr << "port number must be > 1" << std::endl;
					return 1;
				}
				if (port > 65535)
				{
					std::cerr << "port number must be < 65536" << std::endl;
					return 1;
				}
				break;

			case 'a':
				if (strcasecmp(optarg, "verify") == 0)
					action = A_VERIFY;
				else if (strcasecmp(optarg, "iops") == 0)
					action = A_IOPS;
				else if (strcasecmp(optarg, "latency") == 0)
					action = A_LATENCY;
				else
				{
					std::cerr << "-a " << optarg << " is not understood" << std::endl;
					return 1;
				}
				break;

			case 'p':
				dd_perc = atof(optarg);
				if (dd_perc < 0 || dd_perc > 100)
				{
					std::cerr << "-p requires a value between 0 and 100" << std::endl;
					return 1;
				}
				break;

			case 'i':
				sleep_duration = atof(optarg);
				if (sleep_duration <= 0.0)
				{
					std::cerr << "duration must be > 0" << std::endl;
					return 1;
				}
				break;

			case 'n':
				do_reconnect = false;
				break;

			case 'r':
				do_writes = false;
				break;

			case 'f':
				ignore_has_data = true;
				break;

			case 't':
				read_timeout = atof(optarg);
				if (read_timeout <= 0.0)
				{
					std::cerr << "timeout must be > 0" << std::endl;
					return 1;
				}
				break;

			case 'h':
				help();
				return 0;

			default:
				help();
				return 1;
		}
	}

	if (host == NULL)
	{
		std::cerr << "No host to connect to given" << std::endl;
		return 1;
	}

	if (port == -1)
	{
		std::cerr << "No port to connect to given" << std::endl;
		return 1;
	}

	signal(SIGPIPE, SIG_IGN);

	std::cout << "Verifying that the NBD server does not contain any data..." << std::endl;
	if (verify_device_has_no_data(host, port) && ignore_has_data == false)
	{
		std::cerr << "Aborted! (use -f to override this check)" << std::endl;
		return 1;
	}

	USLEEP(useconds_t(sleep_duration * 1000000.0));

	if (action == A_VERIFY)
		return nbd_verify(host, port, sleep_duration, do_reconnect);

	if (action == A_IOPS)
		return nbd_iops(host, port, dd_perc, do_writes);

	if (action == A_LATENCY)
		return nbd_latency(host, port, do_writes);

	return 1;
}
