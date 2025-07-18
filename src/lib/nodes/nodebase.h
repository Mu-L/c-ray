//
//  nodebase.h
//  c-ray
//
//  Created by Valtteri Koskivuori on 07/12/2020.
//  Copyright © 2020-2025 Valtteri Koskivuori. All rights reserved.
//

#pragma once

#include <common/logging.h>
#include <stdbool.h>

#define DUMPBUF_SIZE 16384

// Magic for comparing two nodes

struct node_storage;

// TODO: node_base
struct nodeBase {
	bool (*compare)(const void *, const void *);
	void (*dump)(const void *, char *, int);
};

bool compareNodes(const void *A, const void *B);

#define HASH_CONS(hashtable, hash, T, ...) \
	{ \
		const T candidate = __VA_ARGS__; \
        struct nodeBase *c = (struct nodeBase *)&candidate; \
		char dumpbuf[DUMPBUF_SIZE] = ""; \
		if (c->dump) c->dump(c, dumpbuf, sizeof(dumpbuf)); \
		const uint32_t h = hash(&candidate); \
		const T *existing = findInHashtable(hashtable, &candidate, h); \
		if (existing) {\
			logr(spam, "%s%s%s\n", KGRN, dumpbuf, KNRM);\
			return (void *)existing; \
		} \
		logr(spam, "%s%s%s\n", KRED, dumpbuf, KNRM); \
		insertInHashtable(hashtable, &candidate, sizeof(T), h); \
		return findInHashtable(hashtable, &candidate, h); \
	}
