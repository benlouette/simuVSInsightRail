#ifdef __cplusplus
extern "C" {
#endif

/*
 * configModem.h
 *
 *  Created on: Dec 8, 2015
 *      Author: D. van der Velde
 */

/*
 * Includes
 */
#include "configModem.h"

#include "PinDefs.h"
#include "PowerControl.h"

#include "xTaskModem.h"
#include "Modem.h"
#include "NvmConfig.h"

static void ModemWakeUp(bool wakeup);

bool configModem_PowerOn(void)
{
	return powerModemOn();
}

bool configModem_PowerOff(void)
{
	ModemWakeUp(0);
	powerModemOff();
	return true;// TODO : make sure power is really gone
}

// return true when the modem is poweron according to the modem by testing the modem generated internal power supplies,
// see datasheet for schematic example
bool configModem_PowerCheck(void)
{
    return !GPIO_DRV_ReadPinInput(EHS5E_POWER_IND);
}

bool configModem_connect(char * addr, uint16_t port)
{
    uint8_t serviceProfile  = gNvmCfg.dev.modem.service % MODEM_SERVICEPROFILES;
    uint8_t providerProfile = gNvmCfg.dev.modem.provider % MODEM_PROVIDERPROFILES;

    int rc = Modem_init(
                gNvmCfg.dev.modem.radioAccessTechnology,
                gNvmCfg.dev.modem.providerProfile[providerProfile].simpin,
                gNvmCfg.dev.modem.providerProfile[providerProfile].apn,
                providerProfile,
                serviceProfile,
                (uint8_t *) addr /* gNvmCfg.dev.modem.serviceProfile[serviceProfile].url */,
                port /* gNvmCfg.dev.modem.serviceProfile[serviceProfile].portnr */,
                (uint8_t *) NULL,
                (uint8_t *) NULL,
                NULL,
                NULL,
                gNvmCfg.dev.modem.minimumCsqValue
                );

     // TODO : (12-may-2016) workaround for getting rid of the sometimes appearing extra line feed character after the switch to transparent mode
     if (0 == rc)
     {
         uint8_t dummy;
         uint32_t maxcount = 10;
         while (maxcount-- > 0) {
             // just read 1 char with a timeout of 100ms)
             if (Modem_read(&dummy, 1, 100) == 1)
             {
                 LOG_DBG(LOG_LEVEL_MODEM,"Modem_connect: spurious character after switch to transparent mode : 0x%02x\n", dummy);
             }
             else
             {
                 break;
             }
         }
     }


    return (0 == rc);
}

void configModem_disconnect()
{
    Modem_terminate(gNvmCfg.dev.modem.service % MODEM_SERVICEPROFILES);
}

// function which controls the line to set power on the modem
static void ModemWakeUp(bool wakeup)
{
	if (wakeup) {
		GPIO_DRV_SetPinOutput( EHS5E_AUTO_ON );
	} else {
		GPIO_DRV_ClearPinOutput( EHS5E_AUTO_ON );
	}
}


#ifdef __cplusplus
}
#endif