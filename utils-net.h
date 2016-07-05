ssize_t WRITE(int fd, const char *whereto, size_t len);
ssize_t READ(int fd, unsigned char *whereto, size_t len);
int connect_to(std::string hostname, int port);
