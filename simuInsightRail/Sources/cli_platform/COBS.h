#ifdef __cplusplus
extern "C" {
#endif

/*
 * COBS.h
 *
 *  Created on: 23 Apr 2020
 *      Author: RZ8556
 */

#ifndef SOURCES_CLI_PLATFORM_COBS_H_
#define SOURCES_CLI_PLATFORM_COBS_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define COBS_DELIMITER (0)

inline bool COBS_IsDelimiter(const uint8_t c)
{
	return c == COBS_DELIMITER;
}

size_t COBS_decodeMessage(const uint8_t* in_message, uint8_t* out_message, size_t max_length);
size_t COBS_encodeMessage(const uint8_t*in_message, uint8_t* out_message, size_t length);

#endif /* SOURCES_CLI_PLATFORM_COBS_H_ */


#ifdef __cplusplus
}
#endif