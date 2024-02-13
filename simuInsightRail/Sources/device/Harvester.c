#ifdef __cplusplus
extern "C" {
#endif

/*
 * Harvester.c
 *
 *  Created on: 29 Aug 2019
 *      Author: RZ8556
 */

#include <string.h>
#include "printGDF.h"
#include "PinDefs.h"
#include "PinConfig.h"
#include "CLIcmd.h"
#include "Harvester.h"
#include "linker.h"
#include "Log.h"

static bool harvesterEnable_CLI(uint32_t args, uint8_t * argv[], uint32_t * argi);

static bool m_bIsHarvesterEnabled = true;

static const struct cliCmd harvesterSpecificCommands[] =
{
	{"Harvester",	"\t\t\t0/1 to Disable/Enable the harvester", harvesterEnable_CLI, 0},
};

/*
 * Call this if we are a Harvester and have a command console
 */
void Harvester_InitCLI()
{
	EnableHarvester(true);
	(void)cliRegisterCommands(harvesterSpecificCommands,
			sizeof(harvesterSpecificCommands)/sizeof(*harvesterSpecificCommands));
}

/*
 * EnableHarvester
 *
 * @desc    Set harvester state
 *
 * @param   bEnableHarvester: true to enable harvester, false to disable harvester
 *
 * @returns -
 */
void EnableHarvester(bool bEnableHarvester)
{
	PORT_HAL_SetMuxMode(g_portBase[GPIO_EXTRACT_PORT(HARVESTER_HALT)], GPIO_EXTRACT_PIN(HARVESTER_HALT), kPortPinDisabled);
	pinConfigDigitalOut(HARVESTER_HALT, kPortMuxAsGpio, (bEnableHarvester)?0:1, false);

	m_bIsHarvesterEnabled = bEnableHarvester;
}

/*
 * Local functions
 */

static bool harvesterEnable_CLI(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	if (args == 0)
	{
		 printf("Harvester is %sabled\n", (m_bIsHarvesterEnabled)?"en":"dis");
	}
	else
	{
		EnableHarvester((argi[0] == 1));
	}
	return true;
}



#ifdef __cplusplus
}
#endif