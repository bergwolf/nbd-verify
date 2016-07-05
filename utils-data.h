uint64_t bytes_to_u64(const unsigned char *in);
void u64_to_bytes(unsigned char *p, uint64_t what);
uint32_t bytes_to_u32(const unsigned char *in);
void u32_to_bytes(unsigned char *p, uint32_t what);
void hex_dump(const unsigned char *in, int size);
void get_random_bytes(unsigned char *p, int len);
uint64_t get_random_block_offset(uint64_t n_blocks);
