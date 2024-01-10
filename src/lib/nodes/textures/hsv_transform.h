//
//  hsv_transform.h
//  c-ray
//
//  Created by Valtteri Koskivuori on 09/01/2024.
//  Copyright © 2024 Valtteri Koskivuori. All rights reserved.
//

#pragma once

const struct colorNode *newHSVTransform(const struct node_storage *s, const struct colorNode *tex, const struct valueNode *H, const struct valueNode *S, const struct valueNode *V, const struct valueNode *f);
