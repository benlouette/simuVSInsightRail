#ifdef __cplusplus
extern "C" {
#endif

/*
 * COBS.c
 *
 *  Created on: 23 Apr 2020
 *      Author: RZ8556
 */

#include <stdbool.h>
#include "COBS.h"

/*
 * Decode a buffer using COBS
 */
size_t COBS_decodeMessage(const uint8_t* in_message, uint8_t* out_message, size_t max_length)
{
	uint8_t byCodeVal = (uint8_t)(COBS_DELIMITER - 1);
	uint8_t byCopyVal = 0;
	uint8_t* start = out_message;

	for (int i = 0; i < max_length; byCopyVal--)
	{
		if (byCopyVal != 0)
		{
			*out_message++ = in_message[i++];
		}
		else
		{
			if (byCodeVal != (uint8_t)(COBS_DELIMITER - 1))
			{
				*out_message++ = COBS_DELIMITER;
			}

			byCopyVal = byCodeVal = in_message[i++];
			if (byCodeVal == COBS_DELIMITER)
			{
				// too long!
				break;
			}
		}
	}
	return out_message - start;
}


/*
 * Encode a buffer using COBS
 */
#define StartBlock() (code_ptr = out_message++, delim_offset = 1)
#define FinishBlock() (*code_ptr = delim_offset)

size_t COBS_encodeMessage(const uint8_t* in_message, uint8_t* out_message, size_t length)
{
	uint8_t *start = out_message,
	        *end = (uint8_t*)in_message + length;

	uint8_t delim_offset, *code_ptr;

	StartBlock();
	while(in_message < end)
	{
		if(delim_offset != 0xFF)
		{
			uint8_t c = *in_message++;
			if(c != COBS_DELIMITER)
			{
				*out_message++ = c;
				++delim_offset;
				continue;
			}
		}
		FinishBlock();
		StartBlock();
	}
	FinishBlock();
	return out_message - start;
}




#ifdef __cplusplus
}
#endif