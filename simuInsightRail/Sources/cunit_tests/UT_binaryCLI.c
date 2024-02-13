#ifdef __cplusplus
extern "C" {
#endif

/*
 * UT_binaryCLI.c
 *
 *  Created on: 23 Apr 2020
 *      Author: RZ8556
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "UnitTest.h"
#include "linker.h"
#include "COBS.h"
#include "binaryCLI.h"

static void test_Checksums(void);
static void test_COBS_small(void);
static void test_COBS_gt_256(void);
static void test_COBS_encoding(void);
static void test_COBS_random(void);

CUnit_suite_t UTbinaryCLI = {
	{"bincli", NULL, NULL, CU_TRUE, "test binary CLI functions"},
	{
		{"test checksum handling", test_Checksums},
		{"test COBS encode/decode: small strings", test_COBS_small},
		{"test known string", test_COBS_gt_256},
		{"test COBS encoding", test_COBS_encoding},
		{"test COBS encode/decode: random large strings", test_COBS_random},
		{NULL, NULL}
	}
};


static void test_Checksums(void)
{
	uint8_t *test_string = (uint8_t*)(__sample_buffer);

	for(int i=0; i<20; i++)
	{
		uint8_t test_value = rand() % 256;

		test_string[0] = test_value;
		binaryCLI_generateChecksum(test_string, 1);
		CU_ASSERT(test_string[1] == (test_value ^ CHECKSUM_TARGET));
	}

	for(int i=0; i<20; i++)
	{
		const int length = rand() % 1000;

		uint8_t *p_test_data = test_string;
		for(int j=0; j<length; j++)
		{
			*p_test_data++ = rand() % 256;
		}

		p_test_data = test_string;
		binaryCLI_generateChecksum(test_string, length);
		CU_ASSERT(binaryCLI_verifyChecksum(p_test_data, length + 1));
	}
}


static void test_COBS_small(void)
{
#define MAX_TEST_CASES (3)
	const struct {uint8_t test_string[4]; size_t size;} tests[MAX_TEST_CASES] =
	{
		{{0x55}, 1},
		{{0x55, 0x55}, 2},
		{{0x11, 0x22, 0x55, 0x33}, 4}
	};

	// build test packets with delimiter at end
	for(int i=0; i<MAX_TEST_CASES; i++)
	{
		const size_t test_size = tests[i].size;
		size_t len = COBS_encodeMessage(tests[i].test_string, (uint8_t*)__sample_buffer, test_size);
		*(uint8_t*)(__sample_buffer + len) = COBS_DELIMITER;
		COBS_decodeMessage((uint8_t*)__sample_buffer, (uint8_t*)(__sample_buffer + 10), len);
		CU_ASSERT(memcmp(tests[i].test_string, (uint8_t*)(__sample_buffer + 10), test_size) == 0);
	}
}


static void test_COBS_gt_256(void)
{
#define TEST_STRING_SIZE (260)
	uint8_t *test_string = (uint8_t*)(__sample_buffer);
	uint8_t *decoded_message = (uint8_t*)(__sample_buffer + 1000);
	uint8_t *out_buffer = (uint8_t*)(__sample_buffer + 2000);

	for(int i=0; i<TEST_STRING_SIZE; i++)
	{
		test_string[i] = i + 10; // move wraparound away from 'natural' point
	}

	size_t len = COBS_encodeMessage(test_string, out_buffer, TEST_STRING_SIZE);
	out_buffer[len++] = COBS_DELIMITER;

	size_t decoded_len = COBS_decodeMessage(out_buffer, decoded_message, len);
	decoded_message[decoded_len++] = COBS_DELIMITER;

	CU_ASSERT(memcmp(test_string, decoded_message, TEST_STRING_SIZE) == 0);
}


static void test_COBS_encoding(void)
{
	uint8_t *source_buffer = (uint8_t*)__sample_buffer,
			*work_buffer = (uint8_t*)(__sample_buffer + 2000);

	const int min_test_string_length = 300;
	const int max_test_string = 1500;

	for(int i=0; i<100; i++)
	{
		const int length = min_test_string_length + (rand() % (max_test_string - min_test_string_length));
		uint8_t *p_test_data = source_buffer;

		for(int j=0; j<length; j++)
		{
			*p_test_data++ = rand() % 256;
		}

		size_t encoded_size = COBS_encodeMessage(source_buffer, work_buffer, length);

		// check there are no delimiters in encoded data
		CU_ASSERT(memchr(work_buffer, COBS_DELIMITER, encoded_size - 1) == 0);
	}
}


static void test_COBS_random(void)
{
	uint8_t *source_buffer = (uint8_t*)__sample_buffer,
			*work_buffer = (uint8_t*)(__sample_buffer + 2000),
			*target_buffer = (uint8_t*)(__sample_buffer + 4000);
	const int min_test_string_length = 300;
	const int max_test_string = 1500;

	for(int i=0; i<20; i++)
	{
		const int length = min_test_string_length + (rand() % (max_test_string - min_test_string_length));

		for(int j=0; j<length; j++)
		{
			source_buffer[j] = rand() % 256;
		}

		size_t encoded_len = COBS_encodeMessage(source_buffer, work_buffer, length);
		work_buffer[encoded_len] = COBS_DELIMITER;

		size_t decoded_len = COBS_decodeMessage(work_buffer, target_buffer, encoded_len + 1);
		target_buffer[decoded_len] = COBS_DELIMITER;

		CU_ASSERT(memcmp(source_buffer, target_buffer, length) == 0);
	}
}


#if 0
static void show_data(const uint8_t *buf, const size_t len)
{
	printf("\n\n");
	for(int i=0; i<len; i++)
	{
		if((i % 20) == 0)
		{
			printf("%04d  ", i);
		}
		printf("%02X ", *buf++);
		if(((i + 1) % 20) == 0)
		{
			printf("\n");
		}
		else
		{
			if(((i + 1) % 10) == 0)
			{
				printf("  ");
			}
		}
	}
	printf("\n\n");
}
#endif







#ifdef __cplusplus
}
#endif