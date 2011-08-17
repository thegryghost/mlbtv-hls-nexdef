#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <time.h>
#include <fcntl.h>
#include <curl/curl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <pthread.h>
#include <inttypes.h>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <getopt.h>
#include <unistd.h>
#include <libconfig.h>

#include "mlb.h"

#define HLS_CFG_PROXY						"proxy_addr"
#define HLS_CFG_PLAYER_CMD					"player_cmd"
#define HLS_CFG_LW_CMD						"lw_time"

#define HLS_PLAYLIST_REFRESH_TIME			12

#define HLS_START_MARKER					"#EXTM3U"
#define HLS_KEY_MARKER						"#EXT-X-KEY:"
#define HLS_SEGMENT_LEN_MARKER				"#EXTINF:"
#define HLS_DATETIME_MARKER					"#EXT-X-PROGRAM-DATE-TIME:"
#define HLS_END_MARKER						"#EXT-X-ENDLIST"
#define HLS_BANDWIDTH_MARKER				"BANDWIDTH="
#define HLS_AES128_DESC						"METHOD=AES-128"

#define HLS_HEADER_POS						0
#define HLS_DATETIME_POS					3
#define HLS_FIRSTKEY_POS					5

#define MLB_HLS_DEFAULT_BUFSIZE				(562500 * 10)
//#define MLB_HLS_DEFAULT_BITRATE				3000000

#define MLB_HLS_TIME_FORMAT					"%FT%T" // Ignore timezone info

#define MLB_HLS_DEFAULT_CFGFILE				"mlb.cfg"
//#define MPLAYER_STREAM_CMD					"/usr/bin/mplayer2 -really-quiet -fs -vc ffh264vdpau, -vo vdpau:fps=60:deint=0:denoise=1:hqscaling=1,xv, -livepause 4 -delay 0 -demuxer lavf -mc 2"
#define MPLAYER_STREAM_CMD					" "

int tmp_sw = 1;


// *********************************
void mlb_print_master(MLB_HLS_MASTER_URL * master)
{
	if (master)
	{
		int i;
		printf("\n");
//		printf("[MLB] Base64 (original): %s\n", master->args->base64_uri);
//		printf("[MLB] Base64 (decoded): %s\n", master->b64_decoded);
		printf("[MLB] Master URL: %s\n", master->master_url);
		printf("[MLB] Base URL: %s\n", master->base_url);
		printf("[MLB] Decryption Key: ");
		for (i = 0; i < 16; i++)
		{
			printf("%02x", master->dec_key[i]);
		}
		printf("\n");
		printf("[MLB] Params: %s\n", master->params);

		for(i=0; i < master->stream_count; i++)
		{
			printf("[MLB] Stream #%d - %s - %d [%s]\n", i, master->streams[i].base_url, master->streams[i].bandwidth, master->streams[i].base_url_media);
		}
		printf("\n");
	}
}

void mlb_print_iv(MLB_HLS_IV_STRUCT *m)
{
	int j;
	printf("IV Struct at pos (%d): (0x) ", m->pos);
	for (j = 0; j < AES128_KEY_SIZE; j++)
	{
		printf("%02x", m->iv[j]);
	}
	if (m->aes)
	{
		printf(" AES: (0x) ");
		for (j = 0; j < AES128_KEY_SIZE; j++)
		{
			printf("%02x", m->aes[j]);
		}
	}
	printf("\n");
}

void mlb_print_aes(MLB_HLS_STREAM_URL *m)
{
	int j;
	printf("AES Key: (0x) ");
	for (j = 0; j < AES128_KEY_SIZE; j++)
	{
		printf("%02x", m->aes_key[j]);
	}
	printf("\n");
}


// *********************************

void *mlb_cmd_thread(void *t)
{
	if (t)
	{
		MLB_HLS_MASTER_URL * master = (MLB_HLS_MASTER_URL *)t;
		int ed;
//		printf("[MLB] Launching cmd thread (%s)\n", master->cmd_params);
		ed = system(master->cmd_params);
		printf("[MLB] command done, stopping everything now...\n");
		master->do_loop = 0;
	}
}

size_t mlb_playlist_curl_handler(void *buffer, size_t size, size_t nmemb, void *userp)
{
	MLB_HLS_STREAM_URL *stream = (MLB_HLS_STREAM_URL*) userp;

	if (buffer && size && nmemb && stream && (stream->playlist_size  <  (nmemb + stream->playlist_size)))
	{
		stream->playlist = realloc(stream->playlist, stream->playlist_size + nmemb);
		memcpy(stream->playlist + stream->playlist_size , buffer, nmemb);
		stream->playlist_size += nmemb;
	}
	return nmemb;
}


void mlb_refresh_playlists(MLB_HLS_MASTER_URL * master)
{
	if (master && master->stream_count > 0)
	{
		int i;
		CURLcode res;

		for(i=0; i < master->stream_count; i++)
		{
			if (master->streams[i].state != MLB_HLS_STATE_LIVE)
			{
				printf("[MLB] Skipping Playlist: %s (reason: %s)\n", master->streams[i].base_url,  MLB_STATE_STRINGS[master->streams[i].state]);
				continue;
			}

			if (!master->streams[i].playlist_curl)
				master->streams[i].playlist_curl = curl_easy_init();

			if (master->streams[i].playlist_curl)
			{
				char *tmp_url = calloc(1, MAX_STR_LEN);
				sprintf(tmp_url, "%s%s\0", master->base_url, master->streams[i].base_url);
//				printf("[MLB] Refreshing Stream #%d - %s - %d (%d)\n", i, tmp_url, master->streams[i].bandwidth, master->streams[i].playlist_size);

				if (master->streams[i].playlist)
				{
					free(master->streams[i].playlist);
					master->streams[i].playlist_size = 0;
					master->streams[i].playlist = NULL;
				}

				if (strlen(master->args->proxy_addr) > 5)
					curl_easy_setopt(master->streams[i].playlist_curl, CURLOPT_PROXY, master->args->proxy_addr);
				curl_easy_setopt(master->streams[i].playlist_curl, CURLOPT_URL, tmp_url);
				curl_easy_setopt(master->streams[i].playlist_curl, CURLOPT_WRITEFUNCTION, mlb_playlist_curl_handler);
				curl_easy_setopt(master->streams[i].playlist_curl, CURLOPT_WRITEDATA, (void*)&master->streams[i]);
				res = curl_easy_perform(master->streams[i].playlist_curl);

				curl_easy_cleanup(master->streams[i].playlist_curl);
				master->streams[i].playlist_curl = NULL;

				if (tmp_url)
					free(tmp_url);
			}
		}
	}
}


void mlb_master_sort_streams(MLB_HLS_MASTER_URL * master)
{
	int i, j, tmp, stuff[master->stream_count];

	for (i=0; i < master->stream_count; i++)
		stuff[i] = master->streams[i].bandwidth;

	for (i=0; i < master->stream_count-1; i++)
		if (stuff[i+1] < stuff[i])
		{
			tmp = stuff[i];
			stuff[i] = stuff[i+1];
			stuff[i+1] = tmp;
		}

	j = master->stream_count - 1;
	for (i=0; i < master->stream_count; i++)
		for (tmp=0; tmp < master->stream_count; tmp++)
			if (master->streams[tmp].bandwidth == stuff[i])
			{
				master->streams[tmp].priority = j--;
				break;
			}

	if (master->args->bandwidth_max)
		for (i=0; i < master->stream_count; i++)
			if (master->streams[i].bandwidth > master->args->bandwidth_max)
				master->streams[i].priority = -1;

	for (i=0; i < master->stream_count; i++)
		if (master->streams[i].priority > -1)
			if (master->current_priority == -1)
				master->current_priority = i;
			else if (master->streams[i].priority  < master->streams[master->current_priority].priority)
				master->current_priority = i;

	if (master->args->bandwidth_start)
	{
		for (i=0; i < master->stream_count; i++)
		{
			if (master->streams[i].priority > -1 &&
				master->streams[i].bandwidth == master->args->bandwidth_start)
			{
				master->current_priority = i;
			}
		}
	}
	
	printf("[MLB] Current Priority: %d (bw: %d)\n", master->current_priority, master->streams[master->current_priority].bandwidth);

	for (i=0; i < master->stream_count; i++)
		printf("bw: %d, prio: %d\n", master->streams[i].bandwidth, master->streams[i].priority);

//	master->current_priority = 5;
}


void *mlb_refresh_playlists_thread(void *t)
{
	size_t file_check_sz = 0;
	time_t file_check_time = time(NULL);

	if (t)
	{
		int i, loop=1;
		MLB_HLS_MASTER_URL * master = (MLB_HLS_MASTER_URL *)t;
		printf("[MLB] Playlist refresh time: %d (s)\n", master->args->refresh_time);

		while (loop && master->do_loop)
		{
			pthread_mutex_lock(&master->playlist_mutex);
			mlb_refresh_playlists(master);

			for (i=0; i < master->stream_count; i++)
				if (master->streams[i].state == MLB_HLS_STATE_LIVE)
					break;

			if (i >= master->stream_count)
				loop = 0;

			pthread_mutex_unlock(&master->playlist_mutex);

			if (master->args->output.name && 1)
			{
				struct stat st1;

				memset(&st1, 0, sizeof(struct stat));
				stat(master->args->output.name, &st1);

				if (file_check_time && file_check_sz)
				{
					time_t t_tmp = time(NULL);

					if (t_tmp - file_check_time > (master->args->refresh_time*2))
					{
						if (st1.st_size == file_check_sz)
						{
							loop = 0;
							master->do_loop = 0;
							continue;
						}
						file_check_time = t_tmp;
					}
				}

				file_check_sz = st1.st_size;
				printf("-- TS Filesize: %" PRId64 "\n", file_check_sz);
			}

			if (loop)
			{
				for(i = 0; i < master->args->refresh_time*4; i++)
				{
					if (!master->do_loop)
						break;
					SLEEP_250MS
				}
			}
		}
	}
	printf("[MLB] Stream thread exiting\n");
}


// *********************************

MLB_HLS_IV_STRUCT * mlb_getiv_from_pos(MLB_HLS_STREAM_URL* stream, int pos)
{
	MLB_HLS_IV_STRUCT  * ret = NULL;
	if (stream && pos >0)
	{
		int i;
		for(i=0; i < stream->iv_count; i++)
		{
			if (stream->iv_keys[i].pos == pos)
			{
				ret = &stream->iv_keys[i];
				break;
			}
		}
	}
	return ret;
}

int mlb_stream_getline(MLB_HLS_STREAM_URL* stream, int line_num, char * fill, int fill_len)
{
	int ret = -1;

	if (stream && line_num > 0)
	{
		TEXTFILE_IN_MEMORY *m = memfile_init(stream->playlist, stream->playlist_size);
		if (m)
		{
			int i;
			char *tmp;
			for(i=1; i <= line_num; i++)
			{
				tmp = memfile_getnext_line(m, 0);
				if (tmp == NULL)
					break;
			}

			if (i == (line_num+1))
			{
				strncpy(fill, tmp, fill_len);
				ret = i;
			}
			free(m);
		}
	}
	return ret;
}

size_t mlb_url_decryptor(void *buffer, size_t size, size_t nmemb, void *userp)
{
	MLB_URL_PASS *dec = (MLB_URL_PASS*)userp;

	if (buffer && size && nmemb && dec)
	{
		if (dec->write_pos + nmemb < dec->write_size)
		{
			memcpy(dec->write_buf + dec->write_pos, buffer, nmemb);
			dec->write_pos += nmemb;
		}
		else
			printf("mlb_url_decryptor: out of memory!\n");

	}
	return nmemb;
}

size_t mlb_key_curl_handler(void *buffer, size_t size, size_t nmemb, void *userp)
{
	MLB_HLS_STREAM_URL *stream = (MLB_HLS_STREAM_URL*) userp;

	if (stream && stream->parent && buffer && nmemb)
	{
		char * _tmp = (char *)buffer;
		MLB_HLS_MASTER_URL * master = stream->parent;
		AES_KEY key;

		memset(&key, 0, sizeof(AES_KEY));
		b64decode(_tmp);

		AES_set_decrypt_key(master->dec_key, 128, &key);
		AES_decrypt(_tmp, stream->aes_key, &key);

	}
	return nmemb;
}

void mlb_get_hls_key(MLB_HLS_STREAM_URL *stream)
{
	if (stream)
	{
		CURLcode res;
		if (!stream->key_curl)
			stream->key_curl = curl_easy_init();

		if (stream->key_curl)
		{
			if (strlen(stream->parent->args->proxy_addr) > 5)
				curl_easy_setopt(stream->key_curl, CURLOPT_PROXY, stream->parent->args->proxy_addr);
			curl_easy_setopt(stream->key_curl, CURLOPT_URL, stream->hls_key_url);
			curl_easy_setopt(stream->key_curl, CURLOPT_WRITEFUNCTION, mlb_key_curl_handler);
			curl_easy_setopt(stream->key_curl, CURLOPT_WRITEDATA, (void*)stream);
			res = curl_easy_perform(stream->key_curl);
/*
			if (!res)
			{
			}
*/
			curl_easy_cleanup(stream->key_curl);
			stream->key_curl = NULL;
		}
	}
}

size_t mlb_master_curl_handler(void *buffer, size_t size, size_t nmemb, void *userp)
{
	int i=0, loop = 1;
	char *line2 = (char *)buffer;
	TEXTFILE_IN_MEMORY *m = memfile_init(line2, (int)nmemb);
	MLB_HLS_MASTER_URL * master = (MLB_HLS_MASTER_URL *)userp;

//	printf("Lines: %d\n", m->line_count);

	line2 = memfile_getnext_line(m, 1);

	if (strncmp(line2, HLS_START_MARKER, strlen(HLS_START_MARKER)) == 0)
	{
		char * line = NULL;
		char *bw = NULL;
		i = 1;

		do
		{
			line = memfile_getnext_line(m, 1);
			if (line)
			{
				bw = strstr(line, HLS_BANDWIDTH_MARKER);
				if (bw)
				{
					char * path = memfile_getnext_line(m, 1);
					if (path)
					{
						bw += strlen(HLS_BANDWIDTH_MARKER);
						mlb_master_add_stream(master, path, atoi(bw));
//						printf("%d - %s -- %s\n", i, path, bw);
						i++;
					}
				}
				bw = NULL;
				i++;
			}
			else
				loop = 0;
		} while (loop);
	}
	else
	{
		printf("[MLB] First Line in M3U8 is not %s: %s\n", HLS_START_MARKER, line2);
	}

	free(m);
	return nmemb;
}

void _mlb_deinit_master(MLB_HLS_MASTER_URL * m)
{
	if (m)
	{
		pthread_mutex_destroy(&m->playlist_mutex);

		if (m->media_in)
			free(m->media_in);
		if (m->media_out)
			free(m->media_out);

		if (m->args)
			free(m->args);
	}
}

MLB_HLS_MASTER_URL * _mlb_init_master(void)
{
	MLB_HLS_MASTER_URL * ret = calloc(1, sizeof(MLB_HLS_MASTER_URL));
	if (ret)
	{
		int i;
		ret->current_priority = -1;
		for(i=0; i < MLB_HLS_MAX_STREAMS; i++)
		{
            ret->streams[i].state = MLB_HLS_STATE_LIVE;
            ret->streams[i].cache = 1;
            ret->streams[i].parent = ret;
            ret->streams[i].priority = -1;
		}

		if (pthread_mutex_init(&ret->playlist_mutex, NULL))
		{
			printf("[MLB] pthread_mutex_init error!\n");
		}

		ret->do_loop = 1;
		ret->media_size = MLB_HLS_DEFAULT_BUFSIZE;
		ret->media_in = calloc(1, ret->media_size);
		ret->media_out = calloc(1, ret->media_size);
		ret->media_pos = 0;
	}
	return ret;
}

MLB_HLS_MASTER_URL * mlb_get_master(MLB_OPT_ARGS* args)
{
	MLB_HLS_MASTER_URL * ret = NULL;
	if (args)
	{
		ret = _mlb_init_master();
		if (ret)
		{
			char *token;
			char * _tmp = calloc(1,MAX_STR_LEN);
			int i, len;
			ret->args = args;
			len = strlen(ret->args->base64_uri);

			memcpy(ret->b64_decoded, ret->args->base64_uri, len);

			ret->b64_decoded_len = b64decode(ret->b64_decoded);
			memcpy(_tmp, ret->b64_decoded, ret->b64_decoded_len);

			// Master URL
			token = strtok(_tmp, "|");
			if (token)
			{
				strncpy(ret->master_url, token, MAX_STR_LEN);
				strncpy(ret->base_url, token, MAX_STR_LEN);
				for(i = strlen(ret->base_url); i > 0; i--)
				{
					if (ret->base_url[i] == '/' && (i < strlen(ret->base_url)-1))
					{
						ret->base_url[i+1] = '\0';
						break;
					}
				}
			}

			// Decryption Key
			token = strtok(NULL, "|");
			if (token)
			{
				char * _tmp_key = calloc(1, MAX_STR_LEN);

				strcpy(_tmp_key, token);
				b64decode(_tmp_key);

				for (i = 0; i < 16; i++)
				{
					ret->dec_key[i] = (uint8_t)_tmp_key[i];
				}
				if (_tmp_key)
					free(_tmp_key);
			}

			// Params
			token = strtok(NULL, "|");
			if (token)
			{
				strncpy(ret->params, token, MAX_STR_LEN);
			}

			if(_tmp)
				free(_tmp);
		}
	}
	return ret;
}

int mlb_process_stream_key(MLB_HLS_STREAM_URL *stream, char *line, int pos)
{
	MLB_HLS_MASTER_URL * master = stream->parent;
	int ret = 0;

	if (stream && line)
	{
		int i, j=0, z;
		char *ks[3]= {NULL, NULL, NULL};
		char better[32+1];
		char *line_copy = calloc(1, MAX_STR_LEN);

		strcpy(line_copy, line);
		z = strlen(line_copy);

		ks[j++] = line_copy;
		for(i=0; i < z; i++)
		{
			if (line_copy[i] == ',')
			{
				line_copy[i] = '\0';
				ks[j++] = &line_copy[i+1];
			}
		}

		ks[0] += 11;
		ks[1] += 5;
		ks[1][strlen(ks[1])-1] = '\0';

		if (!stream->key_type)
		{
			if (strcmp(ks[0], HLS_AES128_DESC) == 0)
				stream->key_type = MLB_KEY_TYPE_AES128;
			else
				printf("unknown enc method: %s\n", ks[0]);
		}

		if (strlen(stream->hls_key_url) == 0)
		{
			strcpy(stream->hls_key_url, ks[1]);
			strcat(stream->hls_key_url, "&");
			strcat(stream->hls_key_url, master->params);
			mlb_get_hls_key(stream);
		}

		for(i=0; i < 32; i++)
			better[i] = ks[2][i+5];
		better[i] = '\0';

		i = str_to_bytes(better, (unsigned char*)&stream->iv_keys[stream->iv_count].iv, AES128_KEY_SIZE);
		stream->iv_keys[stream->iv_count].pos = (pos+1);

		if (line_copy)
			free(line_copy);

		stream->iv_count++;
	}

	return ret;
}

int mlb_master_switch_bw(MLB_HLS_MASTER_URL * master, int down)
{
	int ret = 0;

	if (master->current_seg_line)
	{
		int j, prio;

		if (down >= 1)
			prio = master->streams[master->current_priority].priority + 1;
		else
			prio = master->streams[master->current_priority].priority -  1;

		if (prio < 0 || prio >= master->stream_count)
		{
//			printf("[MLB] priority of %d (prio: %d) is the lowest/highest I can go\n", prio-1, prio);
			return 0;
		}

//		printf("[MLB] New prio: %d, old: %d\n", prio, master->streams[master->current_priority].priority);

		for (j=0; j < master->stream_count; j++)
			if (master->streams[j].priority != -1 && master->streams[j].priority == prio)
				break;

		if (!(j >=  master->stream_count) && master->streams[j].bandwidth >= master->args->bandwidth_min)
		{
			printf("debug, switching from: %d, switching to: %d (index in list: %d)\n", master->streams[master->current_priority].bandwidth, master->streams[j].bandwidth, j);
			master->current_priority = j;
			master->current_iv = mlb_getiv_from_pos(&master->streams[j], master->last_key_line);
			master->current_iv->aes = (uint8_t *)&master->streams[j].aes_key;
//			printf("Last_key pos: %d, \n", master->last_key_line);
			return 1;
		}
		else if (master->streams[j].bandwidth < master->args->bandwidth_min)
		{
			printf("[MLB] Minimum bandiwdth: %d, not switching to: %d\n", master->args->bandwidth_min, master->streams[j].bandwidth);
		}
	}
	return 0;
}



void mlb_process_streams(MLB_HLS_STREAM_URL *stream)
{
	MLB_HLS_MASTER_URL * master = stream->parent;
	TEXTFILE_IN_MEMORY *m = memfile_init(stream->playlist, stream->playlist_size);

	char *line = NULL;
	int i, loop = 1;

//	printf("[MLB] -- Processing playlist, bw: %d Start line: %d\n", stream->bandwidth, stream->line_pos);
	stream->line_count = memfile_getline_count(m);

	for(i=0; i < stream->line_pos; i++)
		memfile_getnext_line(m, 0);

	do
	{
		line = memfile_getnext_line(m, 1);
//		printf("line: %s\n", line);
		if (line)
		{
			switch(stream->line_pos)
			{
				case HLS_HEADER_POS :
					if (strncmp(line, HLS_START_MARKER, strlen(HLS_START_MARKER)) != 0)
					{
						printf("[MLB] (bw: %d) Invalid Playlist (start marker): %s\n", stream->bandwidth, line);
						stream->state = MLB_HLS_STATE_BAD;
						loop = 0;
					}
				break;

				case HLS_DATETIME_POS:
				{
					char *tmp = line + strlen(HLS_DATETIME_MARKER);
					struct tm tm;
					if (strptime(tmp, MLB_HLS_TIME_FORMAT, &tm) != 0)
						stream->start_time = mktime(&tm);
					else
						printf("strptime ... ERRORRORORORORO\n");
//					printf("year: %d; month: %d; hour: %d; min: %d; sec: %d\n", 1900 +  tm.tm_year, 1+tm.tm_mon, tm.tm_hour, tm.tm_min, tm.tm_sec);
				}
				break;

				case HLS_FIRSTKEY_POS:
					mlb_process_stream_key(stream, line, stream->line_pos);
				break;

				default:
					if (line[0] == '#' && line[1] == 'E' && line[2] == 'X' && line[3] == 'T')
					{
						if (strstr(HLS_END_MARKER, line) != NULL)
						{
							printf("[MLB] END OF PLAYLIST! (rate: %d)\n", stream->bandwidth);
							stream->state = MLB_HLS_STATE_END;
							loop = 0;
						}
						else if (strstr(line, HLS_KEY_MARKER) != NULL)
						{
							mlb_process_stream_key(stream, line, stream->line_pos);

						}
						else if (!stream->seg_time && strstr(line, HLS_SEGMENT_LEN_MARKER) != NULL)
						{
							stream->seg_time = atoi(line + strlen(HLS_SEGMENT_LEN_MARKER));
//							printf("[MLB] Segment time: %d (bw: %d)\n", stream->seg_time, stream->bandwidth);
						}
					}
				break;
			}
			stream->line_pos++;
		}
	} while (line && loop);

//	printf("[MLB] -- Processing playlist done, bw: %d end line: %d\n", stream->bandwidth, stream->line_pos);
}

void mlb_master_add_stream(MLB_HLS_MASTER_URL *m, char *url, int bandwidth)
{
	if (m && url && bandwidth)
	{
		int i;
		for (i=strlen(url) - 1 ; i > 0; i--)
		{
			if (url[i] == '/')
				break;
		}
		strncpy(m->streams[m->stream_count].base_url, url, MAX_STR_LEN);
		strncpy(m->streams[m->stream_count].base_url_media, url, i+1);
		m->streams[m->stream_count++].bandwidth = bandwidth;
	}
}

int mlb_hls_get_and_decrypt(MLB_URL_PASS *p, char *url)
{
	int ret = 0;
	if (p && p->parent && p->stream)
	{
		CURLcode res;
						char content_url[MAX_STR_LEN];
		CURL * _curl = curl_easy_init();
		MLB_HLS_MASTER_URL *master = p->parent;
		MLB_HLS_STREAM_URL *stream = p->stream;

		sprintf(content_url, "%s%s%s\0", master->base_url, stream->base_url_media, url);
//		printf("GET URL: %s\n",content_url);

		if (_curl)
		{
//			printf("[MLB] DEBUG - Fetch start...\n");
			if (strlen(master->args->proxy_addr) > 5)
				curl_easy_setopt(_curl, CURLOPT_PROXY, master->args->proxy_addr);
			curl_easy_setopt(_curl, CURLOPT_URL, content_url);
			curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, mlb_url_decryptor);
			curl_easy_setopt(_curl, CURLOPT_WRITEDATA, (void*)p);

			if (!master->do_loop)
				return -2;

			res = curl_easy_perform(_curl);
//			printf("[MLB] DEBUG - Fetch done...\n");

			if (!res)
			{
				int out_len = 0, lastDecryptLength = 0;
				EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
				EVP_CIPHER_CTX_init(ctx);

				if (EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, master->current_iv->aes, master->current_iv->iv) == 1)
				{
					if (EVP_DecryptUpdate(ctx, master->media_out, &out_len, p->write_buf, p->write_pos) == 1)
					{
						if (EVP_DecryptFinal_ex(ctx, master->media_out + out_len, &lastDecryptLength) == 1)
						{
//							printf("[MLB] DBEUG -- Decrypt good: %s (%d)\n", url, out_len + lastDecryptLength);
							output_handle_write_data(&master->args->output, master->media_out, out_len + lastDecryptLength, 1);
							ret = out_len + lastDecryptLength;
						}
						else
						{
							printf("[MLB] ERROR!!!!!!!!! BAD DECRYPT [1]\n");
							ret = -1;
						}
					}
				}
				else
				{
					printf("[MLB] ERROR!!!!!!!!! BAD DECRYPT [2]\n");
					ret = -2;
				}

				EVP_CIPHER_CTX_cleanup(ctx);
			}

			curl_easy_cleanup(_curl);
			master->seg_count++;
		}
	}
	return ret;
}

void display_usage(char *exe)
{
    int i;
    printf("Usage: %s [options] -B <base64> -o <output_file>\n\n", exe );
    printf("Required:\n");
    printf("\t-B, --base64\t\t\tbase64 url\n");
    printf("\t-o, --ouput\t\t\tFile to output to\n");
    printf("Options:\n");
    printf("\n\t-V, --verbose\t\t\tVerbose output (big V, use multiple times to increase verbosity)\n");
}

static const char *optString = "B:o:s:c:p:l:f:b:m:r:?VhL";

static const struct option longOpts[] =
{
	{ "output", required_argument, NULL, 'o' },
	{ "base64", required_argument, NULL, 'B' },
	{ "bitrate", optional_argument, NULL, 'b' },
	{ "mbit", optional_argument, NULL, 'b' },
	{ "sbitrate", optional_argument, NULL, 's' },
	{ "refresh", optional_argument, NULL, 'r' },
	{ "cfg", optional_argument, NULL, 'c' },
	{ "proxy", optional_argument, NULL, 'p' },
	{ "lw", optional_argument, NULL, 'l' },
	{ "first", optional_argument, NULL, 'f' },
	{ "lock", optional_argument, NULL, 'L' },
	{ "verbose", no_argument, NULL, 'V' },
	{ "help", no_argument, NULL, 'h' },
	{ NULL, no_argument, NULL, 0 }
};

uint8_t get_opts(int argc, char *const argv[], MLB_OPT_ARGS *opts)
{
	int index = 0;
	int opt = 0;

	do
	{
		opt = getopt_long(argc, argv, optString, longOpts, &index);

		switch (opt)
		{
			case 'B':
				strncpy(opts->base64_uri, optarg, MAX_STR_LEN);
				break;

			case 'p':
				strncpy(opts->proxy_addr, optarg, MAX_STR_LEN);
				break;

			case 'L':
//				if (optarg)
					opts->lock_bandwidth = !opts->lock_bandwidth;
				break;

			case 'c':
				{
					config_t cfg;
					config_setting_t *setting;
					config_init(&cfg);

					strncpy(opts->cfg_file, optarg, MAX_STR_LEN);

					if(config_read_file(&cfg, opts->cfg_file))
					{
						const char *str;
						if(config_lookup_string(&cfg, HLS_CFG_PLAYER_CMD, &str))
						{
							strncpy(opts->launch_cmd, str, MAX_STR_LEN);
							printf("[MLB] Setting launch_cmd to: %s\n", opts->launch_cmd);
						}

						if(config_lookup_string(&cfg, HLS_CFG_LW_CMD, &str))
						{
							opts->launch_wait = atoi(str);
							printf("[MLB] Setting launch_wait: %d\n", opts->launch_wait);
						}

						if(config_lookup_string(&cfg, HLS_CFG_PROXY, &str))
						{
							strncpy(opts->proxy_addr, str, MAX_STR_LEN);
							printf("[MLB] Proxy in config: %s\n", opts->proxy_addr);
						}
					}
					else
					{
						printf("[MLB] cfg file not found, using defaults\n");
					}

					config_destroy(&cfg);
				}
				break;

			case 'l':
				opts->launch_wait = atoi(optarg);
				break;

			case 'o':
				opts->output.name = strdup(optarg);
				opts->output.type = OUTPUT_TYPE_FILE;
				opts->output.use_buffer = 0;
				break;

			case 'b':
				opts->bandwidth_max = atoi(optarg);
				break;

			case 'm':
				opts->bandwidth_min = atoi(optarg);
				break;

			case 's':
				opts->bandwidth_start = atoi(optarg);
				break;

			case 'r':
				opts->refresh_time = atoi(optarg);
				break;


			case 'f':
				if (optarg)
					opts->start_pos = atoi(optarg);
				else
					opts->start_pos = 0;
				break;

			case 'V':
				opts->verbose++;
				break;

			case 'h': /* fall-through is intentional */
			case '?':
				display_usage(argv[0]);
				break;

			default:
				/* You won't actually get here. */
				break;
		}
	}
	while (opt != -1);
	return 0;
}

int main (int argc, char *argv[])
{
	MLB_OPT_ARGS *mlb_args;
	int i=0;
	time_t last_segtime1 = 0, last_segtime2 = 0;

	mlb_args = calloc(1, sizeof(MLB_OPT_ARGS));

	mlb_args->lock_bandwidth = 1;
	mlb_args->refresh_time = HLS_PLAYLIST_REFRESH_TIME;
//	mlb_args->bandwidth_max = MLB_HLS_DEFAULT_BITRATE;
	mlb_args->start_pos = 0;
	mlb_args->launch_wait = 30;
	strcpy(mlb_args->launch_cmd, MPLAYER_STREAM_CMD);
	strcpy(mlb_args->cfg_file, MLB_HLS_DEFAULT_CFGFILE);

	if (get_opts(argc, argv, mlb_args))
		return 1;

	if (mlb_args->output.name == NULL || strlen(mlb_args->base64_uri) < 5)
	{
		display_usage(argv[0]);
		return 1;
	}

	printf("[MLB] Reading cfg file: %s\n", mlb_args->cfg_file);

	if (!output_create(&mlb_args->output))
	{
		MLB_HLS_MASTER_URL * master = mlb_get_master(mlb_args);

		if (master)
		{
			printf("[MLB] Output file: %s\n", mlb_args->output.name);
			printf("[MLB] Max. Bandwidth: %d(bps)\n", mlb_args->bandwidth_max);
			printf("[MLB] Min. Bandwidth: %d(bps)\n", mlb_args->bandwidth_min);
			printf("[MLB] Bandwidth Locking: %d\n", mlb_args->lock_bandwidth);
			if (strlen(mlb_args->proxy_addr) > 5)
				printf("[MLB] Using proxy: %s\n", mlb_args->proxy_addr);

			curl_global_init(CURL_GLOBAL_ALL);
			master->curl = curl_easy_init();

			if (master->curl)
			{
				printf("[MLB] Fetching Master URL...\n");

				if (strlen(mlb_args->proxy_addr) > 5)
					curl_easy_setopt(master->curl, CURLOPT_PROXY, mlb_args->proxy_addr);
				curl_easy_setopt(master->curl, CURLOPT_URL, master->master_url);
				curl_easy_setopt(master->curl, CURLOPT_WRITEFUNCTION, mlb_master_curl_handler);
				curl_easy_setopt(master->curl, CURLOPT_WRITEDATA, (void*)master);

				master->res = curl_easy_perform(master->curl);
				mlb_master_sort_streams(master);
//				mlb_print_master(master);
				curl_easy_cleanup(master->curl);



				pthread_create(&master->url_thread, NULL, mlb_refresh_playlists_thread, (void*)master);

				while (master->do_loop)
				{
					pthread_mutex_lock(&master->playlist_mutex);
					for(i=0; i < master->stream_count; i++)
					{
						if (master->streams[i].playlist_size > master->streams[i].last_size)
						{
							mlb_process_streams(&master->streams[i]);
							if (master->streams[i].start_time && !master->stream_start_time)
							{
								master->stream_start_time = master->streams[i].start_time;
								printf("[MLB] Setting stream start time to (epoch): %ld\n", master->stream_start_time);
							}
							master->streams[i].last_size = master->streams[i].playlist_size;
//							printf("(%d) IVCOUNT: %d\n", master->streams[i].bandwidth, master->streams[i].iv_count);
						}

						//master->current_seg_line = master->streams[i].iv_keys[master->streams[i].iv_count-2].pos;

						if (!master->current_seg_line && master->streams[i].iv_count)
						{
							int lc = master->streams[i].line_count;
							int lb = master->streams[i].iv_keys[master->streams[i].iv_count-1].pos;

							if (!master->args->start_pos)
							{
								if (((lc - lb) / 2) -1 >= 2)
									master->current_seg_line = master->streams[i].iv_keys[master->streams[i].iv_count-1].pos;
								else
									master->current_seg_line = master->streams[i].iv_keys[master->streams[i].iv_count-2].pos;
							}
							else
							{
								if (master->args->start_pos < master->streams[i].iv_count)
									master->current_seg_line = master->streams[i].iv_keys[master->args->start_pos - 1].pos;
							}
							printf("[MLB] Start fetching video from line: %d (%d)\n", master->current_seg_line, master->streams[i].line_count);
						}

					}

					if (master->current_seg_line)
					{
						int j;
						char tmp[MAX_STR_LEN];
						MLB_URL_PASS p;

						for(i=0; i < master->stream_count; i++)
						{
							if (!master->do_loop)
								break;

							if (i != master->current_priority)
								continue;

							if (master->current_seg_line > master->streams[i].line_count)
								master->current_seg_line = master->streams[i].iv_keys[master->streams[i].iv_count-1].pos;

							do
							{
								if (master->current_seg_line >= master->streams[i].line_count && master->streams[i].state == MLB_HLS_STATE_LIVE)
									break;

//								printf("Getting: %d (%d)\n", master->current_seg_line, master->streams[i].bandwidth);

								if (mlb_stream_getline(&master->streams[i], master->current_seg_line, (char*)&tmp, MAX_STR_LEN) > 0)
								{
									if (tmp[0] == '#' && tmp[1] == 'E' && tmp[2] == 'X' && tmp[3] == 'T')
									{
										if (strstr(tmp, HLS_KEY_MARKER) != NULL)
										{
											master->current_iv = mlb_getiv_from_pos(&master->streams[i], master->current_seg_line);
											master->current_iv->aes = (uint8_t *)&master->streams[i].aes_key;
											master->last_key_line = master->current_seg_line;
										}
										else if (strstr(tmp, HLS_END_MARKER) != NULL)
										{
											int j;
											if (mlb_master_switch_bw(master, 1))
												printf("[MLB] --------- Stepping UP bandwidth\n");

											master->streams[i].priority = -1;

											for (j=i+1; j < master->stream_count; j++)
												if (master->streams[j].priority > -1)
												{
													master->current_priority = j;
													break;
												}

											if (j >= master->stream_count)
											{
												master->do_loop = 0;
												printf("[MLB] Out of streams.. exiting\n");
											}
										}
									}
									else
									{
										int d = 0;

										memset(&p, 0, sizeof(MLB_URL_PASS));
										p.iv = master->current_iv;
										p.write_buf = master->media_in;
										p.write_size = master->media_size;
										p.write_pos = 0;
										p.parent = master;
										p.stream = &master->streams[i];

										if (master->do_loop)
										{
											time_t t_start, t_stop;

											t_start = time(NULL);
											d = mlb_hls_get_and_decrypt(&p, tmp);
											t_stop = time(NULL);


											last_segtime2 = last_segtime1;
											last_segtime1 = (t_stop - t_start);
											if (master->seg_count > 1)
											{
												master->seg_ftime += (t_stop - t_start);
												//printf("SEGTIME AVG: %ld (%ld %d, Single Segtime: %d)\n", master->seg_ftime  / (long)master->seg_count, (t_stop - t_start), master->seg_count, p.stream->seg_time);
												if (!master->args->lock_bandwidth && p.stream->seg_time)
												{
//													if (master->seg_ftime  / (long)master->seg_count > master->streams[i].seg_time)
													if (t_stop - t_start  > p.stream->seg_time)
													{

														master->seg_fspike = 0;
														if (mlb_master_switch_bw(master, 1))
															printf("[MLB] --------- Stepping down bandwidth\n");
/*
														master->seg_fgood = 0;
														master->seg_fspike++;

														if (master->seg_fspike > 3)
														{
															master->seg_fspike = 0;
															if (mlb_master_switch_bw(master, 1))
																printf("[MLB] --------- Stepping down bandwidth\n");
														}
*/
													}
													else
													{

														master->seg_fgood = 0;
														if ((last_segtime1 + last_segtime2)/2 < p.stream->seg_time)
															if (mlb_master_switch_bw(master, 0))
																printf("[MLB] --------- Stepping up bandwidth\n");
/*
														master->seg_fspike = 0;
														master->seg_fgood++;
														if (master->seg_fgood > 3)
														{
															master->seg_fgood = 0;
															if (mlb_master_switch_bw(master, 0))
																printf("[MLB] --------- Stepping up bandwidth\n");
														}
*/
													}
												}
												//else
												//	printf("[MLB] Locking bandwidth, no switching done\n");
											}

											if (d)
											{
												master->decrypted_size += d;
												master->decrypted_count++;
												printf("[MLB] bytes decrypted: %d (%ds) -- %s (time: %ld -- %d), BW: %d\n", master->decrypted_size, p.stream->seg_time * master->decrypted_count, tmp, (t_stop - t_start), master->seg_count, master->streams[master->current_priority].bandwidth);
//												printf("MO: %s\n", master->args->launch_cmd);
												if (master->args->launch_cmd && strlen(master->args->launch_cmd) > 2)
												{
//													printf("HEre\n");
													if (!master->cmd_thread && p.stream->seg_time * master->decrypted_count >= master->args->launch_wait)
													{
														sprintf(master->cmd_params, "%s -cache %d %s\0", master->args->launch_cmd, 4*(master->decrypted_size/(p.stream->seg_time * master->decrypted_count)) / 1000, master->args->output.name);
														printf("[MLB] Launching.. %s\n", master->cmd_params);
														pthread_create(&master->cmd_thread, NULL, mlb_cmd_thread, (void*)master);
													}
												}
											}
										}
									}
								}
								else
								{
									//printf("I BROKE\n");
									break;
								}

								master->current_seg_line++;
							} while (master->do_loop);
						}

					}
					pthread_mutex_unlock(&master->playlist_mutex);
					SLEEP_500MS
				} // While loop

				pthread_join(master->url_thread, NULL);

				if (master->cmd_thread)
					pthread_join(master->cmd_thread, NULL);
			}

			output_close(&master->args->output);
			_mlb_deinit_master(master);
			curl_global_cleanup();
			free(master);
		} // Master end
	}
	return 0;
}

