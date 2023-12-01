//
//  tests.h
//  C-Ray
//
//  Created by Valtteri Koskivuori on 10/11/2020.
//  Copyright © 2020-2021 Valtteri Koskivuori. All rights reserved.
//

static char *failed_expression;

// Testable modules
#include "perf_fileio.h"
#include "perf_base64.h"

typedef struct {
	char *test_name;
	time_t (*func)(void);
} perf_test;

static perf_test perf_tests[] = {
	{"fileio::load", fileio_load},
	{"base64::bigfile_encode", base64_bigfile_encode},
	{"base64::bigfile_decode", base64_bigfile_decode},
};

#define perf_test_count (sizeof(perf_tests) / sizeof(perf_test))

