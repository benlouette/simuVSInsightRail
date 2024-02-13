#ifdef __cplusplus
extern "C" {
#endif

/*
 * json.c
 *
 *  Created on: Jan 30, 2018
 *      Author: John McRobert
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "PMIC/pmic.h"

#include "freertos.h"
#include "linker.h"
#include "json.h"
#include "printgdf.h"
#include "Device.h"
#include "projdefs.h"
#include "queue.h"

#define FLASH_VECTOR_SIZE		0x410	// note the interrupt table is 0x400 bytes

	const char titleBootloader[] = "Bootloader";
	const char titleApplication[] = "Application";
	const char titlePMIC_Application[] = "PMIC_Application";

/*****************************************************************************
 * This area is for test purposes only.
 * It's located at 0x410 in flash for 2K-16 bytes
 * It should be overwritten by the utility addVersion
 */
#ifdef _MSC_VER
#pragma section(".FlashVersion", read)

	char versiondata[] = {
		"{"
		"  \"Major\":1,"
		"  \"Minor\":1,"
		"  \"Patch\":555,"
		"  \"EntryNull\":null,"
		"  \"EntryTrue\":true,"
		"  \"EntryFalse\":false,"
		"  \"Sha\":\"cab555005b3b396a0e9046b9f4b23addc7e4d435\","
		"  \"CommitDate\":\"2018-01-30\""
		"}"
	};

__declspec(allocate(".FlashVersion")) char versiondata[sizeof(versiondata)];

#else
char versiondata[] __attribute__((section(".FlashVersion"))) = {
		"{"
		"  \"Major\":1,"
		"  \"Minor\":1,"
		"  \"Patch\":555,"
		"  \"EntryNull\":null,"
		"  \"EntryTrue\":true,"
		"  \"EntryFalse\":false,"
		"  \"Sha\":\"cab555005b3b396a0e9046b9f4b23addc7e4d435\","
		"  \"CommitDate\":\"2018-01-30\""
		"}"
};
#endif
static const char nullChar = 0;

/*
 * jsonFindDelimiter
 *
 * @desc    looks for a ',' or '}' delimiter within a given buffer size
 *
 * @param   json	pointer to a JSON string being parsed
 *
 * @returns pointer to the delimiter else a pointer to 0
 */
char *jsonFindDelimiter(char *pJSON)
{
	for(int i = 0; i < (uint32_t)__app_version_size; i++, pJSON++)
	{
		if(*pJSON == ',')
		{
			return ++pJSON;
		}
		if(*pJSON == '}')
		{
			break;
		}
	}
	return (char*)&nullChar;
}

/*
 * jsonFindClosing
 *
 * @desc    finds the closing double quotes within a given buffer size
 *
 * @param   json	pointer to a JSON string being parsed
 *
 * @returns pointer to the double quotes else NULL
 */
char *jsonFindClosing(const char *p)
{
	uint32_t count = (uint32_t)__app_version_size;

	for(; count ; ++p, count--)
	{
		if(*p == '"')
		{
			return ((char *)p);
		}
		if(*p == '\0')
		{
			return (NULL);
		}
	}
	return NULL;
}

/*
 * jsonFetch
 *
 * @desc    returns the first pair if json is set, consecutive pairs when json = NULL
 *
 * @param   json	pointer to a JSON object or NULL
 * @param   name 	pointer to a char array containing search name or to store string name
 *                  of entry when scanning entries
 * @param   value 	pointer to a char array to store the found value
 * @param   search  boolean set to true searching for entry stored in "name"
 *
 * @returns JSON type or JSON_EOF if no further entries
 */
JSON_types_t jsonFetch(char *json, char *name, void *value, bool search)
{
	static char *pJSON = NULL;

	// is it the first time?
	if(json)
	{
		pJSON = json;
		if(*pJSON != '{')
		{
			return JSON_EOF;	// first character should always be '{'
		}
	}

	int count = (uint32_t)__app_version_size;
	while(count-- && (isprint((int)*pJSON) || (*pJSON == '\n') || (*pJSON == '\r') || (*pJSON == '\t')))
	{
		// strip spaces
		while((*pJSON == ' ') || (*pJSON == '{') || (*pJSON == '}'))
		{
			pJSON++;
		}
		int size;
		char *p;
		JSON_types_t type;

		if(*pJSON == '"')
		{
			pJSON++;
			p = jsonFindClosing(pJSON);
			if(NULL == p)
			{
				return JSON_EOF;
			}
			size = (int)p - (int)pJSON;
			if(search)
			{
				if((strlen(name) != size) || (strncmp(pJSON, name, size) != 0))
				{
					// skip to next entry
					// note if we have a string with a comma we're in trouble here
					pJSON = jsonFindDelimiter(pJSON);
					continue;
				}
			}
			else
			{
				strncpy(name, pJSON, size);
				name[size] = 0;
			}
			pJSON = (p + 1);
			while((*pJSON == ' ') || (*pJSON == ':') || (*pJSON == '\t'))
			{
				pJSON++;
			}
			if(*pJSON == 'n')
			{
				// it's a null
				type = JSON_null;
			}
			else if((*pJSON == 't') || (*pJSON == 'f'))
			{
				// it's a boolean true/false'
				*(bool*)value = (*pJSON == 't');
				type = JSON_boolean;
			}
			else if((*pJSON == '-') || ((*pJSON >= '0') && (*pJSON <= '9')))
			{
				// it's a value
				*(int*)value = atoi(pJSON);
				type = JSON_value;
			}
			else if(*pJSON == '"')
			{
				// we got a string so return it
				// TODO check we don't exceed the string size
				pJSON++;
				p = jsonFindClosing(pJSON);
				if(p)
				{
					// found matching " so copy to the value string
					size = (int)p - (int)pJSON;
					strncpy(value, pJSON, size);
					((char*)value)[size] = 0;
					pJSON = (p + 1);
					type = JSON_string;
				}
				else
				{
					// no matching " so return EOF
					type = JSON_EOF;
				}
			}
			else
			{
				// who knows what's happening so EOF
				type = JSON_EOF;
			}

			if(type == JSON_EOF)
			{
				// skip to the string terminator to stop any further fetches
				while(isprint((int)*pJSON) || (*pJSON == '\n') || (*pJSON == '\r') || (*pJSON == '\t'))
				{
					pJSON++;
				}
			}
			else
			{
				// things are good so find the comma and bye, bye
				pJSON = jsonFindDelimiter(pJSON);
			}
			return type;
		}
		else
		{
			pJSON++;
		}
	}

	return JSON_EOF;
}

/*
 * getFirmwareFullSemVersionMeta
 *
 * @desc    fetches firmware Sem version from metadata
 *
 * @param   meta	pointer to a JSON metadata block
 *
 * @returns String containing Sem Firmware version info.
 */

char* getFirmwareFullSemVersionMeta(char *meta)
{
	char* pSemVer = (char*)__sample_buffer + (uint32_t)__sample_buffer_size - (uint32_t)__app_version_size;

	if(JSON_string != jsonFetch(meta, "FullSemVer", pSemVer, true))
	{
		sprintf(pSemVer, "FullSemVer not found");
	}

	return pSemVer;
}

/*
 * getFirmwareVersionMeta
 *
 * @desc    fetches firmware version from metadata
 * 			TODO version offset is added for backwards compatibility
 * 			- remove as soon as full semantic versioning is used
 *
 * @param   meta	pointer to a JSON metadata block
 *
 * @returns 32 bit firmware version
 */

uint32_t getFirmwareVersionMeta(char *meta)
{
	uint32_t FirmwareVersion = 0, major = 0, minor = 0, patch = 0;

	if((JSON_value == jsonFetch(meta, "Major", &major, true)) &&
	   (JSON_value == jsonFetch(meta, "Minor", &minor, true)) &&
	   (JSON_value == jsonFetch(meta, "Patch", &patch, true)))
	{
		FirmwareVersion = ((major << 24) & 0xFF000000) |
						  ((minor << 16) & 0x00FF0000) |
						  ((patch <<  0) & 0x0000FFFF);
	}

	return FirmwareVersion;
}


/*
 * getFirmwareVersion
 *
 * @desc    get the application version number
 *
 * @param   none
 *
 * @returns version number
 */
uint32_t getFirmwareVersion(void)
{
	// The server side of the system is expecting the patch data to be (patch * 2)
	// where the LSB is then used to determine if it's a release or debug version
	// of the APP. This isn't used but for legacy we adjust it here.
	uint32_t fw = getFirmwareVersionMeta(__app_version);
	fw = (fw & 0xFFFF0000) + ((fw & 0x00007fff) << 1);
	return fw;
}

/*
 * metadataDisplay
 *
 * @desc    display the metadata in bootloader/application version flash
 *
 * @param   title	title "Bootloader/Application"
 * @param   meta 	pointer to the JSON object containing the version information
 * @param   brief	true for short version
 *
 * @returns nothing
 */
void metadataDisplay(const char *title, char *meta, bool brief)
{
	char name[64];
	char value[128];

	// check for a blank flash or no metadata present
	if((uint8_t)meta[0] == 0xFF || meta[0] != '{')
	{
		printf("%s Version information not available\n", title);
	}
	else
	{
		// brief or full display required
		if(brief)
		{
			char sha[16];

			// extract the short Sha value if present
			if(JSON_string == jsonFetch(meta, "Sha", value, true))
			{
				value[8] = 0;
			}
			else
			{
				strcpy(value, "XXXXXXXX");
			}
			strcpy(sha, value);

			// extract the short CommitDate value if present
			if(JSON_string != jsonFetch(meta, "CommitDate", value, true))
			{
				strcpy(value, "1970-01-01");
			}

			// extract Sem version from the object
			printf("%s Version \"%s\"  %s  (%s)\n",
					title,
					getFirmwareFullSemVersionMeta(meta),
					value,
					sha);
		}
		else
		{
			// display the title and dump the metadata
			printf("%s version:\n", title);

			JSON_types_t type = jsonFetch(meta, name, value, false);
			while(type != JSON_EOF)
			{
				switch(type)
				{
				case JSON_null:
					printf("  %s = null\n", name);
					break;
				case JSON_boolean:
					printf("  %s = %s\n", name, (*(bool*)&value ? "true" : "false"));
					break;
				case JSON_value:
					printf("  %s = %d\n", name, *(int*)&value);
					break;
				case JSON_string:
					printf("  %s = %s\n", name, value);
					break;
				default:
					printf("\n");
					return;
				}

				// fetch next entry
				type = jsonFetch(NULL, name, value, false);
			}
		}
	}
}

/*
 * metadataDisplay
 *
 * @desc    display the metadata for the bootloader handling vector remapping
 *
 * @param   full    display full metadata when set
 *
 * @returns nothing
 */
void metadataDisplayLoader(bool full)
{
	// Display Bootloader & Application versions
	if(memcmp((char*)__loader_origin,(char*)__app_origin, FLASH_VECTOR_SIZE) == 0)
	{
		printf("Bootloader not present, vectors remapped\n");
	}
	else
	{
		metadataDisplay(titleBootloader, __loader_version, full);
	}
}

bool cliVer( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	bool rc_ok = true;

	if(args == 0)
	{
		if(Device_HasPMIC())
		{
			metadataDisplayLoader(true);
			metadataDisplay(titleApplication, __app_version, true);
			metadataDisplay(titlePMIC_Application, PMIC_getMetadata(), true);
		}
		else
		{
			metadataDisplayLoader(true);
			metadataDisplay(titleApplication, __app_version, true);
		}
	}
	else if(args == 1)
	{
		if(strncmp((char*)argv[0], "-l", 2) == 0)
		{
			// Display the bootloader version, optional full dump
			metadataDisplayLoader((char)(argv[0][2]) != 'f');
		}
		else if(strncmp((char*)argv[0], "-a", 2) == 0)
		{
			// Display the application version, optional full dump
			metadataDisplay(titleApplication, __app_version, (char)(argv[0][2]) != 'f');
		}
		else if(Device_HasPMIC() && (strncmp((char*)argv[0], "-p", 2) == 0))
		{
			// Display the PMIC version, optional full dump
			metadataDisplay(titlePMIC_Application, PMIC_getMetadata(), (argv[0][2] != 'f'));
		}
		else if(strncmp((char*)argv[0], "-f", 2) == 0)
		{
			// Display the PMIC, bootloader & application versions, optional full dump
			metadataDisplayLoader((char)(argv[0][2]) != 'f');
			metadataDisplay(titleApplication, __app_version, (char)(argv[0][2]) != 'f');
			if(Device_HasPMIC())
			{
				// Display the PMIC version, optional full dump
				metadataDisplay(titlePMIC_Application, PMIC_getMetadata(), (argv[0][2] != 'f'));
			}
		}
		else if(strcmp((char*)argv[0], "-h") == 0)
		{
			// help them people out there
			printf( "ver\tdisplay version info\n"
					"  ver -l(f)\tdisplay bootloader version (optional full)\n"
					"  ver -a(f)\tdisplay application version (optional full)\n"
					"  ver -p(f)\tdisplay PMIC version (optional full)\n"
					"  ver -f(f)\tdisplay App, Loader & PMIC versions (optional full)\n");
		}
	}


	return rc_ok;
}


#ifdef __cplusplus
}
#endif