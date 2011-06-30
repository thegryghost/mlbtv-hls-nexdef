#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "output.h"

#define OUTPUT_FILE_MODE                0666
#define DEFAULT_BIND_IP                 "0.0.0.0"
#define DEFAULT_BIND_PORT               4000
#define DEFAULT_MAX_CONNECTIONS         10
#define DEFAULT_SELECT_TIMEOUT          250     // in ms
#define DEFAULT_WRITE_BUFFER_SIZE       (188*25)


int _output_handle_wait_write(int s, int timeout_sec, int timeout_usec)
{
	fd_set writefds;
	struct timeval tv;
	int rc = 0;

	do
	{
		FD_SET(s, &writefds);
		tv.tv_sec = timeout_sec;
		tv.tv_usec = timeout_usec;
		rc = select(s + 1, NULL, &writefds, NULL, &tv);
		FD_ZERO(&writefds);
	}
	while (rc == 0);
	return rc;
}


#define MAX_WAIT_FOR_HANDLE 8

int output_handle_write_data(OUTPUT_STRUCT * o, uint8_t * data, uint32_t data_len, uint8_t force)
{
	int ret = 0;

	if (o && o->handle != -1)
	{
		uint32_t old_size = 0;

		if (o->use_buffer && data_len > o->write_buffer_size)
		{
			printf("[OUTPUT] Your specified buffer of: %d bytes is too small, disabling buffer\n", o->write_buffer_size);
			output_handle_write_data(o, NULL, 0, 1);
			o->use_buffer = 0;
		}

		if (o->use_buffer)
		{
			if (data_len + o->write_buffer_pos > o->write_buffer_size -1)
			{
				output_handle_write_data(o, NULL, 0, 1);
			}

			if (o->write_buffer_pos < o->write_buffer_size)
			{
				if (data && data_len)
				{
					memcpy(o->write_buffer + o->write_buffer_pos, data, data_len);
					o->write_buffer_pos += data_len;
				}

				if (force)
				{
					old_size = o->write_buffer_size;
					o->write_buffer_size = o->write_buffer_pos;
				}
			}
		}
		else
		{
			o->write_buffer = data;
			o->write_buffer_size = o->write_buffer_pos = data_len;
		}

		if (o->write_buffer_pos >= o->write_buffer_size)
		{
			int wait = 0;
//            printf("[OUTPUT] Buffer full let's write\n");

			while (_output_handle_wait_write(o->handle, 0, 250000) == 0)
			{
				wait++;
				if ( wait >= MAX_WAIT_FOR_HANDLE)
				{
					printf("BIG PROBLEMS WAITING FOR WRITE!\n");
					break;
				}

			}

			if (wait < MAX_WAIT_FOR_HANDLE)
			{
//               printf("[OUTPUT] Waited: %d\n", wait);

				switch (o->type)
				{
					case OUTPUT_TYPE_FILE:
						ret = write(o->handle, o->write_buffer, o->write_buffer_size);
						break;

					case OUTPUT_TYPE_HTTP:
					case OUTPUT_TYPE_RAW_TCP:
						ret =  send(o->handle, o->write_buffer, o->write_buffer_size, 0);
						break;

					case OUTPUT_TYPE_UDP_UNI:
					case OUTPUT_TYPE_UDP_MULTI:

					default:
						printf("[OUTPUT] We don't support UDP yet\n");
						break;
				}

				if (force && old_size)
					o->write_buffer_size = old_size;

				o->write_buffer_pos = 0;
//                printf("... Done, pos: %d sz: %d\n", o->write_buffer_pos, o->write_buffer_size);
			}

		}
//       else
//           printf("[OUTPUT] Buffer not full.. waiting\n");

	}
	return ret;
}

int output_get_file(OUTPUT_STRUCT * ret)
{
	int filehandle = -1;
	if (ret && ret->name)
	{
		if (ret->name[0] == '-')
			ret->handle = filehandle = OUTPUT_STDOUT_FD;
		else
			ret->handle = filehandle = open(ret->name, O_RDWR | O_CREAT | O_TRUNC, OUTPUT_FILE_MODE);
	}

	return filehandle;
}

int output_get_socket(OUTPUT_STRUCT * ret)
{
	int server_socket = -1;

	if (ret)
	{
		socklen_t address_len;
		struct sockaddr_storage address;
		struct addrinfo *addrinfo, hints;
		char portnumber[10];
		int yes = 1;

		sprintf(portnumber, "%d", ret->port);

		memset((void *)&hints, 0, sizeof(hints));
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_ADDRCONFIG | AI_PASSIVE;
		if ((getaddrinfo(ret->name, portnumber, &hints, &addrinfo) != 0) || (addrinfo == NULL))
		{
			printf("[OUTPUT] getaddrinfo() failed..\n");
			return -1;
		}

		if (addrinfo->ai_addrlen > sizeof(struct sockaddr_storage))
		{
			printf("[OUTPUT] Weird, failing..\n");
			freeaddrinfo(addrinfo);
			return -1;
		}

		address_len = addrinfo->ai_addrlen;
		memcpy(&address, addrinfo->ai_addr, addrinfo->ai_addrlen);
		freeaddrinfo(addrinfo);

		server_socket = socket(address.ss_family, SOCK_STREAM, IPPROTO_TCP);
		if (server_socket < 0)
		{
			printf("[OUTPUT] socket() failed\n");
			return -1;
		}

		if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
		{
			printf("[OUTPUT] setsockopt failed\n");
			return -1;
		}

		if (bind(server_socket, (struct sockaddr *) &address, address_len) < 0)
		{
			printf("[OUTPUT] Couldn't bind: %s\n", strerror(errno));
			close(server_socket);
			return -1;
		}
		listen(server_socket, 1);


		printf("[OUTPUT] Listen on IP/Port: %s/%s\n", ret->name, portnumber);
	}
	return server_socket;
}


void *output_rw_thread(void *p)
{
	OUTPUT_STRUCT * ret = (OUTPUT_STRUCT *)p;

	if (ret && ret->server_socket != -1)
	{
		fd_set readfds;
		fd_set writefds;
		struct timeval tv;

		if (ret->max_connections == 0)
			ret->max_connections = DEFAULT_MAX_CONNECTIONS;

		int client_sockets[ret->max_connections];

		struct sockaddr_storage addr;
		socklen_t addrLen = sizeof(struct sockaddr_storage);

		uint8_t socket_count = 0, loop = 1;
		int rc, i, tmp_sock;

		memset(client_sockets, 0, sizeof(int) * ret->max_connections);

		tmp_sock = ret->server_socket;

		printf("[OUTPUT] Server thread started, maximum connections is %d\n", ret->max_connections);

		do
		{
			if (socket_count < ret->max_connections)
				FD_SET(ret->server_socket, &readfds);
			else
				printf("[OUTPUT] Reached the maximum number of sockets\n");

			for (i = 0; i < socket_count; i++)
				if (client_sockets[i])
					FD_SET(client_sockets[i], &readfds);

			tv.tv_sec = 0;
			tv.tv_usec = DEFAULT_SELECT_TIMEOUT * 1000; // micro-seconds

			rc = select(tmp_sock + 1, &readfds, NULL, NULL, &tv);

			if (rc == -1) // Error
			{
				printf("[OUTPUT] Error on select()\n");
				return NULL;
			}
			else if (rc == 0) // No Data, Timeout
			{
			}
			else
			{
				// listen socket, we need to accept() if there's a connection
				if (FD_ISSET(ret->server_socket, &readfds) || FD_ISSET(ret->server_socket, &writefds))
				{
					printf("[OUTPUT] New incoming connection...\n");
					client_sockets[socket_count] = accept(ret->server_socket, (struct sockaddr*) &addr, &addrLen);

					if (client_sockets[socket_count] != -1)
					{

						fcntl(client_sockets[socket_count], F_SETFL, O_NONBLOCK); // set to non-blocking

						if (client_sockets[socket_count] > tmp_sock)
							tmp_sock = client_sockets[socket_count];


						if (ret->cb_connection_established)
							(*ret->cb_connection_established)(ret, client_sockets[socket_count]);
						else
							printf("[OUTPUT] Connection established (no callback)\n");

						socket_count++;
					}
					else
					{
						printf("[OUTPUT] Error with accept()\n");
					}
				}

				for (i = 0; i < socket_count; i++)
				{

					if (FD_ISSET(client_sockets[i], &readfds))
					{
						ssize_t received = 1;

						if (ret->cb_connection_incoming_data)
						{
							received = (*ret->cb_connection_incoming_data)(ret, client_sockets[i]);
						}
						else
						{
							uint8_t buf[1024];
							received = recv(client_sockets[i], buf, 1024, 0);

							printf("[OUTPUT] Data is waiting on socket %d but no callback.. consuming [%x]\n", client_sockets[i], buf[0]);
						}

						if (received <= 0)
						{
							int j;

							if (received == -1)
							{
								printf("[OUTPUT] Error on recv()\n");
							}
							else
							{
								printf("[OUTPUT] Socket closed on the other end\n");
							}

							close(client_sockets[i]);

							if (ret->cb_connection_closed)
								(*ret->cb_connection_closed)(ret, client_sockets[i]);
							else
								printf("[OUTPUT] Connection closed (no callback)\n");

							for (j = i; j < socket_count; j++)
								client_sockets[j] = client_sockets[j+1];
							socket_count--;
						}

					}
				}
			}
			FD_ZERO(&readfds);
		}
		while (loop);

		for (i = 0; i < socket_count; i++)
			close(client_sockets[socket_count]);

		close(ret->server_socket);
	}
	printf("[OUTPUT] Ending socket thread\n");

	return NULL;
}


int8_t output_close(OUTPUT_STRUCT * s)
{
	if (s)
	{
		if (s->handle != -1)
		{
			output_handle_write_data(s, NULL, 0, 1);
			close(s->handle);
			free(s->write_buffer);
			s->write_buffer_size = s->write_buffer_pos = 0;
		}
	}
}

//OUTPUT_STRUCT * output_create(char *c, int port, uint8_t type)
int8_t output_create(OUTPUT_STRUCT * s)
{
	OUTPUT_STRUCT * r = s;
	int8_t ret = 1;

	if (!r)
	{
		r = calloc(1, sizeof(OUTPUT_STRUCT));
		r->name = DEFAULT_BIND_IP;
		r->port = DEFAULT_BIND_PORT;
		r->max_connections = DEFAULT_MAX_CONNECTIONS;
	}

	if (r)
	{
		if (r->use_buffer)
		{
			if (!r->write_buffer_size || r->write_buffer_size % 188)
			{
//                printf("HERE2\n");
				if (!r->write_buffer_size)
				{
					r->write_buffer_size = DEFAULT_WRITE_BUFFER_SIZE;
				}
				else
				{
					r->write_buffer_size = ((r->write_buffer_size / 188)+2) * 188;
				}
			}

			printf("[OUTPUT] Buffering output, Buffer size: %d\n", r->write_buffer_size);
			r->write_buffer = calloc(1, r->write_buffer_size);

			if (r->write_buffer)
			{
				r->write_buffer_pos = 0;
			}
			else
			{
				printf("[OUTPUT] Buffering was specified but cannot malloc(), size: %d\n", r->write_buffer_size);
				r->use_buffer = 0;
			}
		}

		switch (r->type)
		{
			case OUTPUT_TYPE_FILE:
//               printf("[OUTPUT] Writing to file: %s\n", r->name);
				if (output_get_file(r) != -1)
					ret = 0;
				break;

			case OUTPUT_TYPE_HTTP:
			case OUTPUT_TYPE_RAW_TCP:
				printf("[OUTPUT] Creating Listening socket on: %s\n", r->name);
				r->server_socket = output_get_socket(r);
				pthread_create(&r->thread, NULL, output_rw_thread, (void*)r);
				ret = 0;
				break;

			case OUTPUT_TYPE_UDP_UNI:
			case OUTPUT_TYPE_UDP_MULTI:
			default:
				printf("[OUTPUT] We don't support UDP yet\n");
				break;

		}
	}
	return ret;
}

