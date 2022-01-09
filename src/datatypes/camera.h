//
//  camera.h
//  C-ray
//
//  Created by Valtteri Koskivuori on 02/03/2015.
//  Copyright © 2015-2022 Valtteri Koskivuori. All rights reserved.
//

#pragma once

#include "transforms.h"
#include "vector.h"
#include "lightray.h"
#include "spline.h"
#include "quaternion.h"

struct camera {
	float FOV;
	float focal_length;
	float focus_distance;
	float fstops;
	float aperture;
	float aspect_ratio;
	struct coord sensor_size;
	
	struct vector up;
	struct vector right;
	struct vector look_at;
	struct vector forward;
	
	struct transform composite;
	struct euler_angles orientation;
	struct vector position;
	
	struct spline *path;
	float time;
	
	int width;
	int height;
};

void cam_recompute_optics(struct camera *cam);
void cam_update_pose(struct camera *cam, const struct euler_angles *orientation, const struct vector *pos);
struct lightRay cam_get_ray(const struct camera *cam, int x, int y, struct sampler *sampler);
