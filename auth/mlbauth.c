#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#include <pthread.h>
#include <inttypes.h>
#include <getopt.h>
#include <unistd.h>
#include <libconfig.h>
#include <curl/curl.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include "mlbauth.h"

#define MLB_CMARKER_IPID			"ipid"
#define MLB_CMARKER_FPRT			"fprt"
#define MLB_CMARKER_FTMU			"ftmu"

#define MLB_CFG_USERNAME			"username"
#define MLB_CFG_PASSWORD			"password"

#define USER_AGENT 					"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.79 Safari/535.11"

#define MLB_COOKIE_TRIGGER_URL		"http://mlb.com"
#define MLB_LOGIN_URL			 	"https://secure.mlb.com/account/topNavLogin.jsp"
#define MLB_LOGOUT_URL 				"https://secure.mlb.com/enterworkflow.do?flowId=registration.logout&c_id=mlb"
#define MLB_WORKFLOW_URL			"http://www.mlb.com/enterworkflow.do?flowId=media.media"
#define MLB_PLAYBACK_URL			"https://secure.mlb.com/pubajaxws/bamrest/MediaService2_0/op-findUserVerifiedEvent/v-2.3?"
#define MLB_SCENARIO_FILTER			"HTTP_CLOUD_WIRED"

#define MLB_LOGIN_OPTS				"emailAddress=%s&password=%s"
#define MLB_PLAYBACK_OPTS			"sessionKey=%s&identityPointId=%s&contentId=%s&playbackScenario=%s&eventId=%s&fingerprint=%s"

#define DEF_COOKIE_FILENAME			"cookies.txt"
#define DEF_CFG_FILENAME			"mlbauth.cfg"

char event_id[MAX_STR_LEN] 			= {0};
char content_id[MAX_STR_LEN] 		= {0};

char username[MAX_STR_LEN] 			= {0};
char password[MAX_STR_LEN] 			= {0};

char cookie_filename[MAX_STR_LEN] 	= {0};
char cfg_filename[MAX_STR_LEN] 		= {0};

char base64_url[MAX_STR_LEN]		= {0};
char innings_url[MAX_STR_LEN]		= {0};

time_t game_start_time				= {0};

MLB_AUTH_STRUCT auth_struct			= {0};

CURL *curl_handle 					= NULL;
uint8_t curl_inited 				= 0;
uint8_t curl_set_options 			= 1;

static const char *optString = "e:c:j:C:u:p:";

static const struct option longOpts[] =
{
	{ "eid", required_argument, NULL, 'e' },
	{ "cid", required_argument, NULL, 'c' },
	{ "cj", optional_argument, NULL, 'j' },
	{ "cfg", optional_argument, NULL, 'C' },
	{ "user", optional_argument, NULL, 'u' },
	{ "pass", optional_argument, NULL, 'p' },
	{ "help", no_argument, NULL, 'h' },
	{ NULL, no_argument, NULL, 0 }
};

size_t mlb_generic_curl_handler(void *buffer, size_t size, size_t nmemb, void *userp)
{
	MLB_CURL_MEM *carg = NULL;
	if (buffer && size > 0 && nmemb > 0 && userp)
	{
		size_t last_sz = 0;

		carg = (MLB_CURL_MEM *)userp;
		if (!carg->throw_away)
		{
			last_sz = carg->size;

			if (carg->data)
			{
				carg->data = realloc(carg->data, carg->size + nmemb);
				carg->size += nmemb;
			}
			else
			{
				carg->data = malloc(nmemb);
				carg->size = nmemb;
			}
			memcpy(carg->data + last_sz, buffer, nmemb);
		}

	}
	return nmemb;
}


int parse_cookies(CURL *curl, MLB_AUTH_STRUCT *auth)
{
	int i;
	struct curl_slist *cookies = NULL, *nc = NULL;
	char tmp_str[7][MAX_STR_LEN] = {0};
	CURLcode res = 0;

	if (!auth)
	{
		printf("auth is null\n");
		return 2;
	}
	memset(auth, 0, sizeof(MLB_AUTH_STRUCT));

	res = curl_easy_getinfo(curl, CURLINFO_COOKIELIST, &cookies);

	if (res != CURLE_OK)
		return 1;

	nc = cookies;

	while (nc)
	{
		sscanf(nc->data, "%s\t%s\t%s\t%s\t%s\t%s\t%s", &tmp_str[0][0], &tmp_str[1][0],&tmp_str[2][0],&tmp_str[3][0],&tmp_str[4][0],&tmp_str[5][0],&tmp_str[6][0]);
		if (strncmp(&tmp_str[5][0], MLB_CMARKER_IPID, MAX_STR_LEN) == 0)
		{
			strncpy(auth->ipid, &tmp_str[6][0], MAX_STR_LEN);
			auth->ipid_expire = atol(&tmp_str[4][0]);
//			printf("IPID: %s, exp: %" PRId64"\n", auth->ipid, auth->ipid_expire);
		}
		else if (strncmp(&tmp_str[5][0], MLB_CMARKER_FPRT, MAX_STR_LEN) == 0)
		{
			strncpy(auth->fprt, &tmp_str[6][0], MAX_STR_LEN);
			auth->fprt_expire = atol(&tmp_str[4][0]);
//			printf("FPRT: %s, exp: %" PRId64"\n", auth->fprt, auth->fprt_expire);
		}
		else if (strncmp(&tmp_str[5][0], MLB_CMARKER_FTMU, MAX_STR_LEN) == 0)
		{
			strncpy(auth->ftmu, &tmp_str[6][0], MAX_STR_LEN);
			auth->ftmu_expire = atol(&tmp_str[4][0]);
//			printf("FTMU: %s, exp: %" PRId64"\n", auth->ftmu, auth->ftmu_expire);
		}
		nc = nc->next;
	}
	curl_slist_free_all(cookies);
}

uint8_t get_opts(int argc, char *const argv[])
{
	int index = 0;
	int opt = 0;

	do
	{
		opt = getopt_long(argc, argv, optString, longOpts, &index);
		switch (opt)
		{
			case 'e':
				if (optarg)
					strncpy(event_id, optarg, MAX_STR_LEN);
				break;

			case 'c':
				if (optarg)
					strncpy(content_id, optarg, MAX_STR_LEN);
				break;

			case 'j':
				if (optarg)
					strncpy(cookie_filename, optarg, MAX_STR_LEN);
				break;

			case 'C':
				if (optarg)
					strncpy(cfg_filename, optarg, MAX_STR_LEN);
				break;

			case 'h': /* fall-through is intentional */
			case '?':
//				display_usage(argv[0]);
				break;

			default:
				/* You won't actually get here. */
				break;
		}
	}
	while (opt != -1);
	return 0;
}

void cfg_read(void)
{
	config_t cfg;
	config_setting_t *setting;
	config_init(&cfg);

	if(config_read_file(&cfg, cfg_filename))
	{
		const char *str;

		if(config_lookup_string(&cfg, MLB_CFG_USERNAME, &str))
		{
			strncpy(username, str, MAX_STR_LEN);
//			printf("username: %s\n", username);
		}

		if(config_lookup_string(&cfg, MLB_CFG_PASSWORD, &str))
		{
			strncpy(password, str, MAX_STR_LEN);
//			printf("password: %s\n", password);
		}
	}
	else
	{
		printf("cfg file [%s] not found, using defaults\n", cfg_filename);
	}

	config_destroy(&cfg);
}

int mlb_curl_init(void)
{
    if (!curl_inited)
    {
		curl_global_init(CURL_GLOBAL_ALL);
        curl_inited = 1;
    }

	if (!curl_handle)
		curl_handle = curl_easy_init();

	curl_easy_setopt(curl_handle, CURLOPT_COOKIEFILE, cookie_filename);
	curl_easy_setopt(curl_handle, CURLOPT_COOKIEJAR, cookie_filename);

//	curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1);
}

int mlb_login(void)
{
	CURLcode res = {0};
	char tmp_post_data[MAX_STR_LEN] = {0};
	MLB_CURL_MEM carg = {0};

	mlb_curl_init();
	sprintf(tmp_post_data, MLB_LOGIN_OPTS, username, password);
	printf("Login: %s\n", tmp_post_data);

	curl_easy_setopt(curl_handle, CURLOPT_URL, MLB_LOGIN_URL);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, mlb_generic_curl_handler);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)&carg);
	curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, tmp_post_data);

	res = curl_easy_perform(curl_handle);

	if (carg.data)
	{
//		printf("login free: %" PRId64"\n", carg.size);
		free(carg.data);
	}
	return 0;
}

int mlb_wf(void)
{
	CURLcode res = {0};
	MLB_CURL_MEM carg = {0};

	mlb_curl_init();

	curl_easy_setopt(curl_handle, CURLOPT_URL, MLB_WORKFLOW_URL);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, mlb_generic_curl_handler);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)&carg);

	res = curl_easy_perform(curl_handle);

	if (carg.data)
	{
//		printf("wf free: %" PRId64"\n", carg.size);
		free(carg.data);
	}

	return 0;
}


char * parse_xml1(xmlNodePtr a_node, int count, char * checkstr, int level)
{
	char * ret = NULL;
	xmlNode *cur_node = NULL;

    for (cur_node = a_node; cur_node; cur_node = cur_node->next)
	{
		if (cur_node->type == XML_ELEMENT_NODE)
		{
			if (count == level && strncmp(cur_node->name, checkstr, MAX_STR_LEN) == 0)
			{
				if (cur_node->children)
				{
					ret = calloc(1, MAX_STR_LEN);
					strncpy(ret, cur_node->children->content, MAX_STR_LEN);
					break;
				}
//				printf("MOOOOOO: %s %s\n", cur_node->name, ret);
			}
			if (!ret)
				ret = parse_xml1(cur_node->children, count+1, checkstr, level);
		}
	}

//	printf("ret (%d): %s\n", count, ret);
	return ret;
}

static void
print_element_names(xmlNode * a_node, int count)
{
	xmlNode *cur_node = NULL;

    for (cur_node = a_node; cur_node; cur_node = cur_node->next)
	{
		if (cur_node->type == XML_ELEMENT_NODE)
		{
			printf("node type: Element, name: %s", cur_node->name);
			if (cur_node->children)
				printf(" : %s", cur_node->children->content);
			printf(" [%d]\n", count);
			print_element_names(cur_node->children, count+1);
		}
	}
	printf("\n");
}


int mlb_playback(MLB_AUTH_STRUCT *auth)
{
	CURLcode res = {0};
	char tmp_str[MAX_STR_LEN] = {0}, tmp_str2[MAX_STR_LEN] = {0};
	char *tmp_file = NULL;
	MLB_CURL_MEM carg = {0};

	mlb_curl_init();
	sprintf(tmp_str, MLB_PLAYBACK_OPTS, auth->ftmu, auth->ipid, content_id, MLB_SCENARIO_FILTER, event_id, auth->fprt);
	sprintf(tmp_str2, "%s%s\0", MLB_PLAYBACK_URL, tmp_str);

	curl_easy_setopt(curl_handle, CURLOPT_URL, tmp_str2);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, mlb_generic_curl_handler);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)&carg);

	res = curl_easy_perform(curl_handle);

	if (carg.data)
	{
		int z = 0;
		xmlDocPtr doc = {0};
		xmlNode *root_element = NULL;
		xmlNodePtr firstNode = NULL;
		doc = xmlParseMemory(carg.data, carg.size);

		if (doc)
		{
			char *tmp = NULL;

			tmp = parse_xml1(doc->children, 1, "url", 5);
			strncpy(base64_url, tmp, MAX_STR_LEN);
			free(tmp);

			tmp = parse_xml1(doc->children, 1, "innings-index", 6);
			strncpy(innings_url, tmp, MAX_STR_LEN);
			free(tmp);

//			print_element_names(doc->children, 1);
			xmlFreeDoc(doc);
			xmlCleanupParser();
		}
//		printf("pb free: %" PRId64"\n", carg.size);
		free(carg.data);
	}
	return 0;
}

int mlb_innings_url(void)
{
	CURLcode res = {0};
	MLB_CURL_MEM carg = {0};

	mlb_curl_init();

	curl_easy_setopt(curl_handle, CURLOPT_URL, innings_url);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, mlb_generic_curl_handler);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)&carg);

	res = curl_easy_perform(curl_handle);

	if (carg.data)
	{
		char *tmp = NULL;
		tmp = strstr(carg.data, "start_timecode");
		if (tmp)
		{
			int h=0, m=0, s=0;
			int q = 0;
			tmp += 16;

			for(q = 1; q < 9; q++)
			{
				if (tmp[q] == '"')
				{
					tmp[q] = '\0';
					break;
				}
			}
			sscanf(tmp, "%d:%d:%d", &h, &m, &s);
			game_start_time = (h*60*60) + (m * 60) + s;
//			printf("HI: %s (%" PRId64")\n", tmp, game_start_time);
		}
		printf("innings free: %" PRId64"\n", carg.size);
		free(carg.data);
	}
	return 0;
}

int mlb_cookie_trigger(void)
{
	CURLcode res = {0};
	MLB_CURL_MEM carg = {0};

	carg.throw_away = 1;

	mlb_curl_init();
	curl_easy_setopt(curl_handle, CURLOPT_URL, MLB_COOKIE_TRIGGER_URL);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, mlb_generic_curl_handler);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)&carg);


	res = curl_easy_perform(curl_handle);

	if (carg.data)
	{
		printf("trigger free: %" PRId64"\n", carg.size);
		free(carg.data);
	}

	return 0;
}

void print_epoc(time_t tmp, char *str)
{
	printf("%s [%" PRId64"d,", str, tmp/86400);
	tmp -= (tmp/86400) * 86400;
	printf("%" PRId64"h,", tmp/3600);
	tmp -= (tmp/3600) * 3600;
	printf("%" PRId64"m,", tmp/60);
	tmp -= (tmp/60) * 60;
	printf("%" PRId64"s remaining]\n", tmp);
}

int main (int argc, char *argv[])
{
	uint8_t read_again = 0;
	strncpy(cookie_filename, DEF_COOKIE_FILENAME, MAX_STR_LEN);
	strncpy(cfg_filename, DEF_CFG_FILENAME, MAX_STR_LEN);

    if (get_opts(argc, argv))
        return 1;

	cfg_read();

	if (strlen(content_id) == 0 || strlen(event_id) == 0 || strlen(password) == 0 || strlen(username) == 0)
	{
		printf("Need to specify EventID & contentID and have Username/Password set in CFG file\n");
		return 1;
	}
	printf("EventID: %s, ContentID: %s\n", event_id, content_id);
	printf("CFG File: %s\n", cfg_filename);
	printf("Cookie File: %s\n", cookie_filename);
	printf("Username [%s]\nPassword [%c .. %c]\n", username ,password[0], password[strlen(password)-1]);

	mlb_curl_init();
	mlb_cookie_trigger();
	parse_cookies(curl_handle, &auth_struct);
	auth_struct.current_time = time(NULL);

	if (auth_struct.ipid_expire <= auth_struct.current_time)
	{
		mlb_login();
		read_again = 1;
	}


	if (auth_struct.ftmu_expire <= auth_struct.current_time)
	{
		mlb_wf();
		read_again = 1;
	}

	if (read_again)
		parse_cookies(curl_handle, &auth_struct);

	if (auth_struct.ipid_expire)
	{
		time_t tmp = auth_struct.ipid_expire - auth_struct.current_time;
		print_epoc(tmp, "ipid cookie valid");
	}

	if (auth_struct.ftmu_expire)
	{
		time_t tmp = auth_struct.ftmu_expire - auth_struct.current_time;
		print_epoc(tmp, "ftmu cookie valid");
	}

	mlb_playback(&auth_struct);
//	print_cookies(curl_handle);

	mlb_innings_url();
	printf("Base64_URL: %s\n", base64_url);
	printf("Game Start Time (seconds): %" PRId64"\n", game_start_time);
//	printf("Inning Index: %s\n", innings_index);

	curl_easy_cleanup(curl_handle);
	curl_global_cleanup();

	return 0;
}
