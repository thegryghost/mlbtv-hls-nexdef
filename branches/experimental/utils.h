#ifndef MLB_UTILS
#define MLB_UTILS

#include <stdint.h>
#include <memory.h>

#define SLEEP_1S		usleep(1000000);
#define SLEEP_250MS		usleep(250000);
#define SLEEP_500MS		usleep(500000);


struct textfile_in_memory
{
    int pos;
    int line_count;
    int data_size;
    uint8_t *data;
};
typedef struct textfile_in_memory TEXTFILE_IN_MEMORY;

struct textfile_memline
{
	int line_num;
	char *line_data;
};
typedef struct textfile_memline TEXTFILE_MEMLINE;

int b64decode(char *);
unsigned int htoi (const char *);
int str_to_bytes(char *, unsigned char *, int);

TEXTFILE_IN_MEMORY * memfile_init(char *, int);
char * memfile_getnext_line(TEXTFILE_IN_MEMORY *, int);
int memfile_getline_count(TEXTFILE_IN_MEMORY *);
uint32_t get_time_ms(void);


#endif
