#ifdef __cplusplus
extern "C" {
#endif

/*
 * TestUART.h
 *
 *  Created on: Feb 11, 2016
 *      Author: Bart Willemse
 */

#ifndef TESTUART_H_
#define TESTUART_H_

#include <stdint.h>
#include <stdbool.h>

void TestUARTInit(void);
void TestUARTSendBytes(uint8_t *pBytes, uint8_t NumBytes);

#endif // TESTUART_H_




#ifdef __cplusplus
}
#endif