//
//  hitrecord.h
//  C-Ray
//
//  Created by Valtteri Koskivuori on 06/12/2020.
//  Copyright © 2020-2021 Valtteri Koskivuori. All rights reserved.
//

#pragma once

#include "../../common/vector.h"

struct hitRecord {
	struct vector incident_dir;		//Incident ray direction
	struct vector hitPoint;			//Hit point vector in world space
	struct vector surfaceNormal;	//Surface normal at that point of intersection
	struct coord uv;				//UV barycentric coordinates for intersection point
	const struct bsdfNode *bsdf;	//Surface properties of the intersected object
	struct poly *polygon;			//ptr to polygon that was encountered
	float distance;					//Distance to intersection point
	int instIndex;					//Instance index, negative if no intersection
};
