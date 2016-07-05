extern double read_timeout;

// connect and do handshake
int connect_nbd(std::string host, int port, uint64_t *size, uint32_t *flags, bool verbose);

int send_command_nbd(int fd, uint32_t type, uint64_t handle, uint64_t offset, uint32_t len);
uint32_t verify_ack(int fd, off64_t handle);

uint32_t write_nbd(int fd, off64_t offset, const char *data, size_t len);
uint32_t read_nbd(int fd, off64_t offset, char *data, size_t len);

int flush_nbd(int fd);
int discard_nbd(int fd, uint64_t offset, uint64_t len);

int close_nbd(int fd);
