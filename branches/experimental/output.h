#ifndef MPEG_OUTPUT
#define MPEG_OUTPUT

#include <stdint.h>
#include <sys/socket.h>
#include <pthread.h>
#include "utils.h"

#define OUTPUT_TYPE_FILE                1
#define OUTPUT_TYPE_HTTP                2
#define OUTPUT_TYPE_RAW_TCP             3
#define OUTPUT_TYPE_UDP_UNI             4
#define OUTPUT_TYPE_UDP_MULTI           5

#define OUTPUT_STDIN_FD                 0
#define OUTPUT_STDOUT_FD                1
#define OUTPUT_STDERR_FD                2


static const char *OUTPUT_TYPE_STRINGS[6] =
{
	"None - No output",
	"File",
	"HTTP",
	"Raw TCP",
	"UDP - Unicast",
	"UDP - Multicast",
};

struct output_struct
{
	char * name;
	uint8_t type;
	uint8_t use_http;
	uint8_t max_connections;
	int handle;
	int port;
	pthread_t thread;

	int server_socket;

	uint8_t use_buffer;
	uint8_t * write_buffer;
	uint32_t write_buffer_size;
	uint32_t write_buffer_pos;

	void * extra_data;

	int8_t (*cb_connection_established)(struct output_struct *, int);
	ssize_t (*cb_connection_incoming_data)(struct output_struct *, int);
	int8_t (*cb_connection_closed)(struct output_struct *, int);
//    fd_set readfds;
//    fd_set writefds;
//    struct timeval tv;
};
typedef struct output_struct OUTPUT_STRUCT;

int output_handle_write_data(OUTPUT_STRUCT *, uint8_t *, uint32_t, uint8_t);
//int output_handle_wait_write(int, int, int);
int8_t output_create(OUTPUT_STRUCT *);
int8_t output_close(OUTPUT_STRUCT *);

#endif

