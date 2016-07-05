#include <errno.h>
#include <iostream>
#include <stdint.h>
#include <string>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>

#include "utils-data.h"
#include "utils-net.h"
#include "utils-str.h"
#include "utils-time.h"

double read_timeout = 5.0;

int wait_for_data(int fd)
{
	for(;;)
	{
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		struct timeval tv = { time_t(read_timeout), suseconds_t((read_timeout - int(read_timeout)) * 1000000) };

		int rc = select(fd + 1, &rfds, NULL, NULL, &tv);
		if (rc == -1)
		{
			if (errno == EINTR)
				continue;

			std::cerr << "select() failed because of " << strerror(errno) << std::endl;
			return -1;
		}

		if (rc == 0)
		{
			std::cerr << "timeout while waiting for ack from nbd-server: nbd-server hanging or not sending ack?" << std::endl;
			return -2;
		}

		if (rc == 1 && FD_ISSET(fd, &rfds))
			return 0;

		break;
	}

	return -1;
}

int connect_nbd(std::string host, int port, uint64_t *size, uint32_t *flags, bool verbose)
{
	int fd = -1;

	*flags = -1;
	*size = -1;

	if (verbose)
		std::cout << "connecting to " << host << " " << port << std::endl;

	for(;;)
	{
		fd = connect_to(host, port);
		if (fd != -1)
			break;

		USLEEP(100000);
	}

	if (wait_for_data(fd))
	{
		std::cerr << "timeout waiting for password" << std::endl;
		return -1;
	}

	int rc = -1;
	char password[8 + 1] = { 0 };
	if ((rc = READ(fd, password, 8)) != 8)
	{
		std::cerr << "read error waiting for password (" << rc << " bytes out of 8 received)" << std::endl;
		close(fd);
		return -1;
	}

	if (strcmp(password, "NBDMAGIC"))
	{
		std::cerr << "password mismatch " << password << std::endl;
		close(fd);
		return -1;
	}

	if (wait_for_data(fd))
	{
		std::cerr << "timeout waiting for magic" << std::endl;
		return -1;
	}

	unsigned char magic[8] = { 0 };
	unsigned char expected_magic[8] = { 0x00, 0x00, 0x42, 0x02, 0x81, 0x86, 0x12, 0x53 };
	if ((rc = READ(fd, magic, 8)) != 8)
	{
		std::cerr << "read error waiting for magic (" << rc << " bytes out of 8 received)" << std::endl;
		close(fd);
		return -1;
	}

	if (memcmp(magic, expected_magic, 8))
	{
		std::cerr << "magic mismatch " << std::endl;
		close(fd);
		return -1;
	}

	if (wait_for_data(fd))
	{
		std::cerr << "timeout waiting for size" << std::endl;
		return -1;
	}

	unsigned char size_in[8] = { 0 };
	if ((rc = READ(fd, size_in, 8)) != 8)
	{
		std::cerr << "read error waiting for size (" << rc << " bytes out of 8 received)" << std::endl;
		close(fd);
		return -1;
	}

	*size = bytes_to_u64(size_in);

	if (verbose)
		std::cout << "device size: " << *size << std::endl;

	if (*size < 512)
	{
		std::cerr << "strange device size" << std::endl;
		close(fd);
		return -1;
	}

	if (wait_for_data(fd))
	{
		std::cerr << "timeout waiting for flags" << std::endl;
		return -1;
	}

	unsigned char flags_in[4] = { 0 };
	if ((rc = READ(fd, flags_in, 4)) != 4)
	{
		std::cerr << "read error waiting for flags (" << rc << " bytes out of 4 received)" << std::endl;
		close(fd);
		return -1;
	}

	*flags = bytes_to_u32(flags_in);

	if (verbose)
	{
		if (*flags)
			std::cout << "Flags:" << std::endl;
		if (*flags & 1)
			std::cout << "\thas flags" << std::endl;
		if (*flags & 2)
			std::cout << "\tis R/O" << std::endl;
		if (*flags & 4)
			std::cout << "\tsupports flush" << std::endl;
		if (*flags & 8)
			std::cout << "\tsupports fua" << std::endl;
		if (*flags & 16)
			std::cout << "\tis rotational media" << std::endl;
		if (*flags & 32)
			std::cout << "\tsupports trim" << std::endl;
	}

	if (*flags >= 0x40 || ((*flags & 1) == 0 && *flags > 0))
	{
		std::cerr << "invalid value for flags " << format("%04x", flags) << std::endl;
		close(fd);
		return -1;
	}

	if (wait_for_data(fd))
	{
		std::cerr << "timeout waiting for filler" << std::endl;
		return -1;
	}

	unsigned char filler[124] = { 0 };
	if ((rc = READ(fd, filler, sizeof filler)) != sizeof filler)
	{
		std::cerr << "read error waiting for filler (" << rc << " bytes out of " << sizeof filler << " received)" << std::endl;
		close(fd);
		return -1;
	}

	for(int index=0; index < sizeof filler; index++)
	{
		if (filler[index])
		{
			std::cerr << "encountered != 0 value in filler " << format("%02x", filler[index]) << std::endl;
			close(fd);
			return -1;
		}
	}

	return fd;
}

int send_command_nbd(int fd, uint32_t type, uint64_t handle, uint64_t offset, uint32_t len)
{
	unsigned char cmd[28] = { 0 };

	u32_to_bytes(&cmd[0], 0x25609513);
	u32_to_bytes(&cmd[4], type);
	memcpy(&cmd[8], &handle, 8);
	u64_to_bytes(&cmd[16], offset);
	u32_to_bytes(&cmd[24], len);

	if (WRITE(fd, (const char *)cmd, sizeof cmd) != sizeof cmd)
	{
		std::cerr << "short write sending command header" << std::endl;
		return -1;
	}

	return 0;
}

uint32_t verify_ack(int fd, off64_t handle)
{
	if (wait_for_data(fd))
	{
		std::cerr << "timeout waiting for ack" << std::endl;
		return -1;
	}

	int rc = -1;

	unsigned char ack[16] = { 0 };
	if ((rc = READ(fd, (char *)ack, sizeof ack)) != sizeof ack)
	{
		std::cerr << "short read during ack retrieval (" << rc << " bytes out of " << sizeof ack << " received)" << std::endl;
		return -1;
	}

	uint32_t magic_expected = 0x67446698;
	uint32_t magic = bytes_to_u32(&ack[0]);
	if (magic != 0x67446698)
	{
		std::cerr << "ack magic wrong " << format("%08x", magic) << " (expected: " << format("%08x", magic_expected) << ")" << std::endl;
		return -1;
	}

	if (memcmp(&ack[8], &handle, 8))
	{
		std::cerr << "handle incorrect" << std::endl;
		std::cerr << "expected: ";
		hex_dump((const unsigned char *)&handle, 8);
		std::cerr << std::endl;
		std::cerr << "got: ";
		hex_dump(&ack[8], 8);
		std::cerr << std::endl;
		return -1;
	}

	return bytes_to_u32(&ack[4]);
}

uint32_t write_nbd(int fd, off64_t offset, const char *data, size_t len)
{
	uint64_t handle;

	get_random_bytes((unsigned char *)&handle, sizeof handle);

	if (send_command_nbd(fd, 1, handle, offset, len) == -1)
		return -1;

	if (len > 0 && WRITE(fd, data, len) != len)
	{
		std::cerr << "short write sending data for write-command" << std::endl;
		return -1;
	}

	uint32_t rc = verify_ack(fd, handle);

	return rc;
}

uint32_t read_nbd(int fd, off64_t offset, char *data, size_t len)
{
	uint64_t handle;

	get_random_bytes((unsigned char *)&handle, sizeof handle);

	if (send_command_nbd(fd, 0, handle, offset, len))
		return -1;

	uint32_t rc = verify_ack(fd, handle);

	if (len > 0)
	{
		if (wait_for_data(fd))
		{
			std::cerr << "timeout waiting for data for read-command" << std::endl;
			return -1;
		}

		if (READ(fd, data, len) != len)
		{
			std::cerr << "short read retrieving data for read-command" << std::endl;
			return -1;
		}
	}

	return rc;
}

int close_nbd(int fd)
{
	uint64_t handle;

	get_random_bytes((unsigned char *)&handle, sizeof handle);

	if (send_command_nbd(fd, 2, handle, 0, 0))
		return -1;

	// FIXME wait for an ack?
	// reference implementation does not send it

	close(fd);

	return 0;
}

int flush_nbd(int fd)
{
	uint64_t handle;

	get_random_bytes((unsigned char *)&handle, sizeof handle);

	if (send_command_nbd(fd, 3, handle, 0, 0))
	{
		std::cerr << "failure sending flush command" << std::endl;
		return -1;
	}

	uint32_t rc = verify_ack(fd, handle);

	return rc;
}

int discard_nbd(int fd, uint64_t offset, uint64_t len)
{
	uint64_t handle;

	get_random_bytes((unsigned char *)&handle, sizeof handle);

	if (send_command_nbd(fd, 4, handle, offset, len))
	{
		std::cerr << "failure sending discard command" << std::endl;
		return -1;
	}

	uint32_t rc = verify_ack(fd, handle);

	return rc;
}
