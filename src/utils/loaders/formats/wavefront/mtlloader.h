//
//  mtlloader.h
//  C-ray
//
//  Created by Valtteri Koskivuori on 02/04/2019.
//  Copyright © 2019-2020 Valtteri Koskivuori. All rights reserved.
//

#pragma once

struct material *parseMTLFile(const char *filePath, int *mtlCount);
