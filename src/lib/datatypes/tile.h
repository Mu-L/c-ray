//
//  tile.h
//  C-ray
//
//  Created by Valtteri Koskivuori on 06/07/2018.
//  Copyright © 2018-2022 Valtteri Koskivuori. All rights reserved.
//

#pragma once

#include "../../includes.h"
#include "../../common/dyn_array.h"
#include "../../common/platform/mutex.h"

#include "../../common/vector.h"

enum render_order {
	ro_top_to_bottom = 0,
	ro_from_middle,
	ro_to_middle,
	ro_normal,
	ro_random
};

struct renderer;

enum tile_state {
	ready_to_render = 0,
	rendering,
	finished
};

struct render_tile {
	unsigned width;
	unsigned height;
	struct intCoord begin;
	struct intCoord end;
	enum tile_state state;
	bool network_renderer; //FIXME: client struct ptr
	int index;
	size_t total_samples;
	size_t completed_samples;
};

typedef struct render_tile render_tile;
dyn_array_def(render_tile);

struct tile_set {
	struct render_tile_arr tiles;
	size_t finished;
	struct cr_mutex *tile_mutex;
};

struct tile_set tile_quantize(unsigned width, unsigned height, unsigned tile_w, unsigned tile_h, enum render_order order);
void tile_set_free(struct tile_set *set);

struct render_tile *tile_next(struct tile_set *set);

struct render_tile *tile_next_interactive(struct renderer *r, struct tile_set *set);
