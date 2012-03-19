// http://gdx.mlb.com/components/game/mlb/year_2012/month_03/day_17/grid.xml

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <getopt.h>
#include "mlblist.h"

#define MLB_XML_DATE_FORMAT			"%Y/%m/%d"

#define MLB_XML_EVENT_ID			"calendar_event_id"
#define MLB_XML_EVENT_TIME			"event_time"
#define MLB_XML_ID					"id"
#define MLB_XML_IND					"ind"

#define MLB_XML_AWAY_CODE			"away_code"
#define MLB_XML_AWAY_TEAM_NAME		"away_team_name"
#define MLB_XML_HOME_CODE			"home_code"
#define MLB_XML_HOME_TEAM_NAME		"home_team_name"

#define MLB_XML_CONTENT_ID			"id"
#define MLB_XML_CONTENT_BLACKOUT	"blackout"
#define MLB_XML_CONTENT_DISPLAY		"display"
#define MLB_XML_CONTENT_SCENARIO	"playback_scenario"
#define MLB_XML_PLAYBACK_TYPE		"HTTP_CLOUD_WIRED"

// Level 1
#define MLB_XML_LEVEL1_COUNT		8
static const char *MLB_XML_LEVEL1_PROPS[MLB_XML_LEVEL1_COUNT] =
{
	MLB_XML_EVENT_ID,
	MLB_XML_EVENT_TIME,
	MLB_XML_ID,
	MLB_XML_IND,
	MLB_XML_AWAY_CODE,
	MLB_XML_AWAY_TEAM_NAME,
	MLB_XML_HOME_CODE,
	MLB_XML_HOME_TEAM_NAME,
};

// Level 4
#define MLB_XML_LEVEL4_COUNT		4
static const char *MLB_XML_LEVEL4_PROPS[MLB_XML_LEVEL4_COUNT] =
{
	MLB_XML_CONTENT_SCENARIO,
	MLB_XML_CONTENT_BLACKOUT,
	MLB_XML_CONTENT_ID,
	MLB_XML_CONTENT_DISPLAY,
};

xmlDocPtr doc = {0};
xmlNode *root_element = NULL;

xmlNodePtr firstNode = NULL;
xmlNodePtr game_level_1 = NULL;
xmlNodePtr game_level_2 = NULL;
xmlNodePtr game_level_3 = NULL;
xmlNodePtr game_level_4 = NULL;

char *input_file = NULL;

char event_id[MAX_STR_LEN];
char event_time[MAX_STR_LEN];
char event_date[MAX_STR_LEN];
char status[MAX_STR_LEN];
char away_code[MAX_STR_LEN];
char away_name[MAX_STR_LEN];
char home_code[MAX_STR_LEN];
char home_name[MAX_STR_LEN];

char content_id[2][MAX_STR_LEN];
char content_display[2][MAX_STR_LEN];


static const char *optString = "i:m:";

static const struct option longOpts[] =
{
    { "mode", optional_argument, NULL, 'm' },
    { "input", required_argument, NULL, 'i' },
    { NULL, no_argument, NULL, 0 }
};

uint8_t get_opts(int argc, char *const argv[])
{
	int index = 0;
	int opt = 0;

	do
	{
		opt = getopt_long(argc, argv, optString, longOpts, &index);
		switch (opt)
		{
			case 'i':
				if (optarg)
				{
					input_file = strdup(optarg);
				}
			break;

			case 'm':
			break;

			default:
			break;
		}
	}
	while (opt != -1);

	return 0;
}

void print_mlb_line(int count)
{
	int i, j;
	char new_date[12] = {0};
	struct tm tm = {0};
	i = strlen(event_date);

//	if (i < 4)
//		return;
	for(j=i; j >= 0; j--)
	{
		if (event_date[j] == '/')
		{
			event_date[j] = '\0';
			break;
		}
	}

	if (strptime(event_date, MLB_XML_DATE_FORMAT, &tm) != 0)
	{
		char new_time[80] = {0};

		strftime(new_time, 80, "%b %-d", &tm);
		printf("%s: ", new_time);
	}

	i = strlen(event_time);
	if (event_time[i-1] == 'M')
	{
		event_time[i-3] = event_time[i-2] + 32;
		event_time[i-2] = '\0';
	}

	printf("%s - %s at %s,", event_time, away_name, home_name);
	for(j=0; j < count; j++)
	{
		if (j>0)
			printf(",");
		printf("%s;%s;%s;%s;%s_%s", content_display[j], event_id, content_id[j], status,away_code, home_code);
	}
	printf("\n");
}


void parse_xml1(void)
{
	xmlChar *myPropValue = NULL;
	const xmlChar *tmp_prop = NULL;
	int i;

	for(firstNode = doc->children;firstNode != NULL; firstNode = firstNode->next)
	{
		for(game_level_1 = firstNode->children; game_level_1 != NULL; game_level_1 = game_level_1->next)
		{
			for (i=0; i < MLB_XML_LEVEL1_COUNT; i++)
			{
				tmp_prop = MLB_XML_LEVEL1_PROPS[i];
				myPropValue = xmlGetProp(game_level_1, tmp_prop);
				if (myPropValue)
				{
//					printf("FOUND [%s] : [%s]\n", MLB_XML_LEVEL1_PROPS[i], myPropValue);
					switch (i)
					{
						case 0:
							strncpy(event_id, myPropValue, MAX_STR_LEN);
						break;

						case 1:
							strncpy(event_time, myPropValue, MAX_STR_LEN);
						break;

						case 2:
							strncpy(event_date, myPropValue, MAX_STR_LEN);
						break;

						case 3:
							strncpy(status, myPropValue, MAX_STR_LEN);
						break;

						case 4:
							strncpy(away_code, myPropValue, MAX_STR_LEN);
						break;

						case 5:
							strncpy(away_name, myPropValue, MAX_STR_LEN);
						break;

						case 6:
							strncpy(home_code, myPropValue, MAX_STR_LEN);
						break;

						case 7:
							strncpy(home_name, myPropValue, MAX_STR_LEN);
						break;
					}


					free(myPropValue);
				}
			}

			for(game_level_2 = game_level_1->children; game_level_2 != NULL; game_level_2 = game_level_2->next)
			{
				if (game_level_2->type == XML_ELEMENT_NODE)
				{
					for(game_level_3 = game_level_2->children; game_level_3 != NULL; game_level_3 = game_level_3->next)
					{
						if (game_level_3->type == XML_ELEMENT_NODE)
						{
							int q = 0;
							for(game_level_4 = game_level_3->children; game_level_4 != NULL; game_level_4 = game_level_4->next)
							{
								if (game_level_4->type == XML_ELEMENT_NODE)
								{
									xmlChar *tmp_char = NULL;
									tmp_char = xmlGetProp(game_level_4, MLB_XML_LEVEL4_PROPS[0]);

									if (tmp_char)
									{
										if (strncmp(tmp_char, MLB_XML_PLAYBACK_TYPE, 30) == 0)
										{
											free(tmp_char);

											tmp_char = xmlGetProp(game_level_4, MLB_XML_LEVEL4_PROPS[1]);
											if (tmp_char)
											{
												if (strstr(tmp_char, "INMARKET") == NULL)
												{
													for (i=2; i < MLB_XML_LEVEL4_COUNT; i++)
													{
														tmp_prop = MLB_XML_LEVEL4_PROPS[i];
														myPropValue = xmlGetProp(game_level_4, tmp_prop);
														if (myPropValue)
														{
															switch (i)
															{
																case 2:
																	strncpy(content_id[q], myPropValue, MAX_STR_LEN);
//																	printf("MOO1: %d -- %s\n", q, content_id[q]);
//																	printf("(%d) HI1: %s\n", i, content_id);
																break;

																case 3:
																	strncpy(content_display[q], myPropValue, MAX_STR_LEN);
//																	printf("MOO2: %d -- %s\n", q, content_display[q]);
//																	printf("(%d) HI2: %s\n", i, content_display);
																break;
															}
															free(myPropValue);
														}
													}
													q++;
												}

												free(tmp_char);
											}
										}
									}
								}
							}
							if (q)
							{
								//printf("DO PRINT MLB\n");
								print_mlb_line(q);

							}
						}
					}
				}
			}
   		}
	}

}

int main(int argc, char * argv[])
{
	if (get_opts(argc, argv) || !input_file)
	{
		printf("Need file, use '-i'\n");
		return 1;
	}
	doc = xmlParseFile(input_file);
	parse_xml1();
	xmlFreeDoc(doc);
	return 0;
}
