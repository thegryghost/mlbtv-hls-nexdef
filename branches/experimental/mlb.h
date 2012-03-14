#ifndef MLB_HLS_H
#define MLB_HLS_H

#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <curl/curl.h>
#include "output.h"
#include "utils.h"

#define AES128_KEY_SIZE			16
#define MAX_STR_LEN				1024
#define MLB_KEY_TYPE_AES128		1
#define MLB_HLS_MAX_STREAMS		12
#define MLB_MAX_IV_COUNT		800

#define MLB_HLS_STATE_END		0
#define MLB_HLS_STATE_LIVE		1
#define MLB_HLS_STATE_BAD		2


static const char *MLB_STATE_STRINGS[3] =
{
	"End of Playlist",
	"Still Live",
	"Invalid Playlist"
};


struct mlb_hls_iv_struct
{
	int pos;
	uint8_t iv[AES128_KEY_SIZE];
};
typedef struct mlb_hls_iv_struct MLB_HLS_IV_STRUCT;

struct mlb_hls_key
{
	int pos;
	char key_url[MAX_STR_LEN];
	uint8_t aes_key[AES128_KEY_SIZE];
	uint8_t haz_aes;
};
typedef struct mlb_hls_key MLB_HLS_KEY;

struct mlb_opt_args
{
	OUTPUT_STRUCT output;
	int bandwidth_max;
	int bandwidth_min;
	int bandwidth_start;
	int lock_bandwidth;
	int refresh_time;
	int start_pos;
	char base64_uri[MAX_STR_LEN];
	int verbose;
	int launch_wait;

	uint32_t last_bps_time[255];
	uint32_t last_bps_segcount_avg;
	uint32_t last_bps_pos;

	char launch_cmd[MAX_STR_LEN];
	char cfg_file[MAX_STR_LEN];
	char proxy_addr[MAX_STR_LEN];

};
typedef struct mlb_opt_args MLB_OPT_ARGS;

struct mlb_hls_stream_url
{
	CURL *playlist_curl;
	CURL *key_curl;
	CURLcode res;

	uint8_t cache;
	int8_t state;
	uint8_t key_type;

	int iv_count;
	MLB_HLS_IV_STRUCT iv_keys[MLB_MAX_IV_COUNT];

	int dec_key_count;
	MLB_HLS_KEY dec_keys[50];

	int line_pos;
	int line_count;

	int playlist_size;
	char *playlist;

	long start_time;
	long start_time_offset;

	char hls_key_url[MAX_STR_LEN];

	char base_url[MAX_STR_LEN];
	char base_url_media[MAX_STR_LEN];
	int bandwidth;

	int last_size;
	int seg_time;
	struct mlb_hls_master_url *parent;

	int priority;
};
typedef struct mlb_hls_stream_url MLB_HLS_STREAM_URL;


struct mlb_hls_master_url
{
	MLB_OPT_ARGS *args;

	CURL *curl;
	CURLcode res;

	uint8_t do_loop;
	uint32_t b64_decoded_len;
	char b64_decoded[MAX_STR_LEN*2];

	char base_url[MAX_STR_LEN];
	char master_url[MAX_STR_LEN];

	uint8_t dec_key[AES128_KEY_SIZE];
	char params[MAX_STR_LEN];

	long stream_start_time;
	int stream_count;

	int current_priority;
	int max_priority;

	int decrypted_size;
	int decrypted_count;
	double decrypted_time;

	int current_seg_line;

	int last_key_line;

	int seg_count;

	MLB_HLS_KEY *current_dec_key;
	MLB_HLS_IV_STRUCT *current_iv;

	MLB_HLS_STREAM_URL streams[MLB_HLS_MAX_STREAMS];

	pthread_t url_thread;
	pthread_mutex_t playlist_mutex;

	int media_size;
	int media_pos;
	uint8_t * media_in;
	uint8_t * media_out;

	pthread_t cmd_thread;
	char cmd_params[MAX_STR_LEN];
};
typedef struct mlb_hls_master_url MLB_HLS_MASTER_URL;


struct mlb_url_pass
{
	MLB_HLS_MASTER_URL *parent;
	MLB_HLS_STREAM_URL *stream;

	int write_size;
	int write_pos;
	uint8_t * write_buf;
};
typedef struct mlb_url_pass MLB_URL_PASS;

struct mlb_curl_mem
{
	size_t size;
	char *data;
};
typedef struct mlb_curl_mem MLB_CURL_MEM;

void mlb_master_add_stream(MLB_HLS_MASTER_URL *, char *, int);
int mlb_get_url(char *, char **, char *proxy);
size_t mlb_get_url_curl(char *, char **, char *proxy);

#endif
