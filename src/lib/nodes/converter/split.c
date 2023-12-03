//
//  split.c
//  C-Ray
//
//  Created by Valtteri Koskivuori on 17/12/2020.
//  Copyright © 2020-2022 Valtteri Koskivuori. All rights reserved.
//

#include <stdio.h>
#include "../../../common/color.h"
#include "../../../common/mempool.h"
#include "../../datatypes/hitrecord.h"
#include "../../../common/hashtable.h"
#include "../../datatypes/scene.h"
#include "../valuenode.h"

#include "split.h"

struct splitValue {
	struct colorNode node;
	const struct valueNode *input;
};

static bool compare(const void *A, const void *B) {
	const struct splitValue *this = A;
	const struct splitValue *other = B;
	return this->input == other->input;
}

static uint32_t hash(const void *p) {
	const struct splitValue *this = p;
	uint32_t h = hashInit();
	h = hashBytes(h, &this->input, sizeof(this->input));
	return h;
}

static void dump(const void *node, char *dumpbuf, int len) {
	struct splitValue *self = (struct splitValue *)node;
	char input[DUMPBUF_SIZE / 2] = "";
	if (self->input->base.dump) self->input->base.dump(self->input, input, sizeof(input));
	snprintf(dumpbuf, len, "splitValue { input: %s }", input);
}

static struct color eval(const struct colorNode *node, sampler *sampler, const struct hitRecord *record) {
	const struct splitValue *this = (struct splitValue *)node;
	float val = this->input->eval(this->input, sampler, record);
	//TODO: What do we do with the alpha here?
	return (struct color){val, val, val, 1.0f};
}

const struct colorNode *newSplitValue(const struct node_storage *s, const struct valueNode *node) {
	HASH_CONS(s->node_table, hash, struct splitValue, {
		.input = node ? node : newConstantValue(s, 0.0f),
		.node = {
			.eval = eval,
			.base = { .compare = compare, .dump = dump }
		}
	});
}

