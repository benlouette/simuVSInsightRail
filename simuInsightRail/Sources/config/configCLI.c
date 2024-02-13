#ifdef __cplusplus
extern "C" {
#endif

/*
 * configCLI.c
 *
 *  Created on: Oct 29, 2015
 *      Author: George de Fockert
 */

#include "configCLI.h"
#include "CLIio.h"

#include "CLIcmd.h"


/*
 * Macros
 */
#if 0
/*
 * Example how to enable a secondary CLI port (RS485 example from SRB22220)
 */
// no secondary port for this project (yet)
#include "HartRs485.h"

static struct cmdLine_str   secundaryCmd;
static const struct cliIo_str  cliIoSecundary = {
		RS485_UART_GetCharsInRxBuf,
		RS485_get_ch,
		RS485_put_ch,
		RS485_put_ch_nb,
		"RS485 port",
		&secundaryCmd
};
#endif

/*
 * Only one CLI configured for passenger rail (UART3)
 */
const  struct cliIo_str *const cliIo[MAX_DEBUG_PUT_CH] = {
		&cliIoMain,
		//&cliIoSecundary
};


#ifdef __cplusplus
}
#endif