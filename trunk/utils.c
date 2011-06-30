#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include "utils.h"
#include <openssl/evp.h>


unsigned int htoi (const char *ptr)
{
	unsigned int value = 0;
	char ch = *ptr;

	while (ch == ' ' || ch == '\t')
		ch = *(++ptr);

	for (;;)
	{
		if (ch >= '0' && ch <= '9')
			value = (value << 4) + (ch - '0');
		else if (ch >= 'A' && ch <= 'F')
			value = (value << 4) + (ch - 'A' + 10);
		else if (ch >= 'a' && ch <= 'f')
			value = (value << 4) + (ch - 'a' + 10);
		else
			return value;
		ch = *(++ptr);
	}
}

int b64decode(char *s)
{
	char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	int bit_offset=0, byte_offset=0, idx=0, i=0, n=0;
	unsigned char *d = (unsigned char *)s;
	char *p;
	while (*s && (p=strchr(b64,*s)))
	{
		idx = (int)(p - b64);
		byte_offset = (i*6)/8;
		bit_offset = (i*6)%8;
		d[byte_offset] &= ~((1<<(8-bit_offset))-1);
		if (bit_offset < 3) {
			d[byte_offset] |= (idx << (2-bit_offset));
			n = byte_offset+1;
		} else {
			d[byte_offset] |= (idx >> (bit_offset-2));
			d[byte_offset+1] = 0;
			d[byte_offset+1] |= (idx << (8-(bit_offset-2))) & 0xFF;
			n = byte_offset+2;
		}
		s++; i++;
	}
	/* null terminate */
	d[n] = 0;
	return n;
}


TEXTFILE_IN_MEMORY * memfile_init(char *data, int len)
{
	TEXTFILE_IN_MEMORY * ret = NULL;
	if (data && len)
	{
		ret = calloc(1, sizeof(TEXTFILE_IN_MEMORY));
		ret->data = (uint8_t *)data;
		ret->data_size = len;
		if (1)
		{
			int i;
			for(i=0; i < len; i++)
			{
				if(ret->data[i] == '\n')
					ret->line_count++;
			}
		}
	}
	return ret;
}

int memfile_getline_count(TEXTFILE_IN_MEMORY *m)
{
	if (m)
		return m->line_count;
	return -1;
}

char * memfile_getnext_line(TEXTFILE_IN_MEMORY *m, int mem)
{
	char *ret = NULL;
	if (m && m->pos < m->data_size)
	{
		int i;

		for(i=m->pos; i < m->data_size; i++)
		{
			if (m->data[i] == '\n' || m->data[i] == '\0')
			{
				m->data[i] = '\0';
				break;
			}
		}

		if (i > m->pos)
		{
			ret = (char *) &m->data[m->pos];
			m->pos = (i+1);
		}
	}
	return ret;
}

int str_to_bytes(char *in, uint8_t *out, int size)
{
	int j = 0;
	if (in && out && size)
	{
		int i;
		int len = strlen(in);
		uint8_t _tmp[3];

		if (len / 2 > len)
		{
			printf("set_hex: Error! out buffer too small: %d (%d)\n", size, len);
			return 0;
		}

		if (len %2)
		{
			printf("set_hex: Error! Uneven hex string\n");
			return 0;
		}

		for(i=0; i < len; i+=2)
		{
			_tmp[0] = (uint8_t)in[i];
			_tmp[1] = (uint8_t)in[i+1];
			_tmp[2] = '\0';

			out[j++] = (uint8_t)htoi((char*)_tmp);
		}
	}
	return j;
}
