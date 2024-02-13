#ifdef __cplusplus
extern "C" {
#endif

/*
 * CLIcmd.h
 *
 *  Created on: Sep 16, 2014
 *      Author: George de Fockert
 */

#ifndef CLICMD_H_
#define CLICMD_H_


typedef bool (* commandFuncPtr)(uint32_t argc, uint8_t * argv[], uint32_t * argi);
struct cliCmd {
	 const char *const cmdstr;
	 const char *const short_helpstr;
	 const commandFuncPtr cmdfunc;
	 const commandFuncPtr long_help;
	 } ;


struct cliSubCmd {
    const char *const subcmdstr;
    const commandFuncPtr cmdfunc;
};

// for the UART redirection
extern const struct cliIo_str  cliIoMain ;

int CLI_handle_command(char * cmdbuf, uint8_t dbg_put_ch);

bool cliSubcommand(uint32_t argc, uint8_t * argv[], uint32_t * argi,  struct cliSubCmd * subComds_p, uint16_t numSubCmds);

bool cliRegisterCommands( const struct cliCmd * cmds, uint32_t numCmds);

#endif /* CLICMD_H_ */


#ifdef __cplusplus
}
#endif