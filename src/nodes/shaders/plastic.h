//
//  plastic.h
//  C-Ray
//
//  Created by Valtteri Koskivuori on 01/12/2020.
//  Copyright © 2020-2021 Valtteri Koskivuori. All rights reserved.
//

#pragma once

const struct bsdfNode *newPlastic(const struct world *world, const struct colorNode *color, const struct valueNode *roughness, const struct valueNode *IOR);
