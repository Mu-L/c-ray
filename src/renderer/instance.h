//
//  instance.h
//  C-ray
//
//  Created by Valtteri Koskivuori on 23.6.2020.
//  Copyright © 2020-2022 Valtteri Koskivuori. All rights reserved.
//

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "../datatypes/transforms.h"
#include "../utils/mempool.h"
#include "samplers/sampler.h"

struct world;

struct lightRay;
struct hitRecord;

struct sphere;
struct mesh;

struct instance {
	struct transform composite;
	int bsdf_count;
	const struct bsdfNode **bsdfs;
	struct color *emissions;
	bool (*intersectFn)(const struct instance *, const struct lightRay *, struct hitRecord *, sampler *);
	void (*getBBoxAndCenterFn)(const struct instance *, struct boundingBox *, struct vector *);
	void *object;
};

struct instance new_sphere_instance(struct sphere *sphere, float *density, struct block **pool);
struct instance new_mesh_instance(struct mesh *mesh, float *density, struct block **pool);

bool isMesh(const struct instance *instance);

void addInstanceToScene(struct world *scene, struct instance instance);
