#ifdef __cplusplus
extern "C" {
#endif

/*!
** @file HF.h
** @version 01.00
** @brief
**          Component to simplify hard faults for ARM/Kinetis.
*/         

#ifndef __HF_H
#define __HF_H

/*
 * Includes
 */
#include <stdint.h>

/*
 * Functions
 *
 */

void HardFault_Handler(void);
/*
** ===================================================================
**     Method      :  HardFault_Handler (component HardFault)
**     Description :
**         Hard Fault Handler
**     Parameters  : None
**     Returns     : Nothing
** ===================================================================
*/

#ifdef __GNUC__ /* 'used' attribute needed for GNU LTO (Link Time Optimization) */
void HF_HandlerC(uint32_t *hardfault_args) __attribute__((used));
#else
void HF_HandlerC(uint32_t *hardfault_args);
#endif
/*
** ===================================================================
**     Method      :  HF_HandlerC (component HardFault)
**
**     Description :
**         Additional handler which decodes the processor status
**         This method is internal. It is used by Processor Expert only.
** ===================================================================
*/

/* END HF. */

#endif
/* ifndef __HF_H */


#ifdef __cplusplus
}
#endif