ssize_t WRITE(int fd, const void *whereto, size_t len);
ssize_t READ(int fd, void *whereto, size_t len);
int connect_to(std::string hostname, int port);
