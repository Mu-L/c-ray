//
//  renderer.c
//  C-Ray
//
//  Created by Valtteri Koskivuori on 19/02/2017.
//  Copyright © 2017 Valtteri Koskivuori. All rights reserved.
//

#include "includes.h"
#include "renderer.h"

#include "camera.h"
#include "light.h"
#include "poly.h"
#include "scene.h"
#include "raytrace.h"

/*
 * Global renderer
 */
struct renderer mainRenderer;

#ifdef WINDOWS
HANDLE tileMutex = INVALID_HANDLE_VALUE;
#else
pthread_mutex_t tileMutex;
#endif

bool renderTilesEmpty() {
	return mainRenderer.renderedTileCount >= mainRenderer.tileCount;
}

/**
 Gets the next tile from renderTiles in mainRenderer
 
 @return A renderTile to be rendered
 */
struct renderTile getTile() {
#ifdef WINDOWS
	WaitForSingleObject(tileMutex, INFINITE);
#else
	pthread_mutex_lock(&tileMutex);
#endif
	//FIXME: This could be optimized
	struct renderTile tile = mainRenderer.renderTiles[mainRenderer.renderedTileCount++];
	mainRenderer.renderTiles[mainRenderer.renderedTileCount - 1].isRendering = true;
	tile.tileNum = mainRenderer.renderedTileCount - 1;
	
#ifdef WINDOWS
	ReleaseMutex(tileMutex);
#else
	pthread_mutex_unlock(&tileMutex);
#endif
	return tile;
}

/**
 Create tiles from render plane, and add those to mainRenderer
 
 @param worldScene worldScene object
 */
void quantizeImage(struct scene *worldScene) {
#ifdef WINDOWS
	//Create this here for now
	tileMutex = CreateMutex(NULL, FALSE, NULL);
#endif
	printf("Quantizing render plane...\n");
	int tilesX = worldScene->camera->width / worldScene->camera->tileWidth;
	int tilesY = worldScene->camera->height / worldScene->camera->tileHeight;
	
	float tilesXf = (float)worldScene->camera->width / (float)worldScene->camera->tileWidth;
	float tilesYf = (float)worldScene->camera->height / (float)worldScene->camera->tileHeight;
	
	if (tilesXf - (int)tilesXf != 0) {
		tilesX++;
	}
	if (tilesYf - (int)tilesYf != 0) {
		tilesY++;
	}
	
	mainRenderer.renderTiles = (struct renderTile*)calloc(tilesX*tilesY, sizeof(struct renderTile));
	
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			struct renderTile *tile = &mainRenderer.renderTiles[x + y*tilesX];
			tile->width  = worldScene->camera->tileWidth;
			tile->height = worldScene->camera->tileHeight;
			
			tile->startX = x       * worldScene->camera->tileWidth;
			tile->endX   = (x + 1) * worldScene->camera->tileWidth;
			
			tile->startY = y       * worldScene->camera->tileHeight;
			tile->endY   = (y + 1) * worldScene->camera->tileHeight;
			
			tile->endX = min((x + 1) * worldScene->camera->tileWidth, worldScene->camera->width);
			tile->endY = min((y + 1) * worldScene->camera->tileHeight, worldScene->camera->height);
			
			tile->completedSamples = 1;
			tile->isRendering = false;
			tile->tileNum = mainRenderer.tileCount;
			
			mainRenderer.tileCount++;
		}
	}
	printf("Quantized image into %i tiles. (%ix%i)", (tilesX*tilesY), tilesX, tilesY);
}

void reorderTopToBottom() {
	int endIndex = mainRenderer.tileCount - 1;
	
	struct renderTile *tempArray = (struct renderTile*)calloc(mainRenderer.tileCount, sizeof(struct renderTile));
	
	for (int i = 0; i < mainRenderer.tileCount; i++) {
		tempArray[i] = mainRenderer.renderTiles[endIndex--];
	}
	
	free(mainRenderer.renderTiles);
	mainRenderer.renderTiles = tempArray;
}

void reorderFromMiddle() {
	int midLeft = 0;
	int midRight = 0;
	bool isRight = true;
	
	midRight = ceil(mainRenderer.tileCount / 2);
	midLeft = midRight - 1;
	
	struct renderTile *tempArray = (struct renderTile*)calloc(mainRenderer.tileCount, sizeof(struct renderTile));
	
	for (int i = 0; i < mainRenderer.tileCount; i++) {
		if (isRight) {
			tempArray[i] = mainRenderer.renderTiles[midRight++];
			isRight = false;
		} else {
			tempArray[i] = mainRenderer.renderTiles[midLeft--];
			isRight = true;
		}
	}
	
	free(mainRenderer.renderTiles);
	mainRenderer.renderTiles = tempArray;
}

void reorderToMiddle() {
	int left = 0;
	int right = 0;
	bool isRight = true;
	
	right = mainRenderer.tileCount - 1;
	
	struct renderTile *tempArray = (struct renderTile*)calloc(mainRenderer.tileCount, sizeof(struct renderTile));
	
	for (int i = 0; i < mainRenderer.tileCount; i++) {
		if (isRight) {
			tempArray[i] = mainRenderer.renderTiles[right--];
			isRight = false;
		} else {
			tempArray[i] = mainRenderer.renderTiles[left++];
			isRight = true;
		}
	}
	
	free(mainRenderer.renderTiles);
	mainRenderer.renderTiles = tempArray;
}

/**
 Reorder renderTiles in given order
 
 @param order Render order to be applied
 */
void reorderTiles(enum renderOrder order) {
	switch (order) {
		case renderOrderFromMiddle:
		{
			reorderFromMiddle();
		}
			break;
		case renderOrderToMiddle:
		{
			reorderToMiddle();
		}
			break;
		case renderOrderTopToBottom:
		{
			reorderTopToBottom();
		}
			break;
		default:
			break;
	}
}

/**
 Gets a pixel from the render buffer
 
 @param worldScene WorldScene to get image dimensions
 @param x X coordinate of pixel
 @param y Y coordinate of pixel
 @return A color object, with full color precision intact (double)
 */
struct color getPixel(struct scene *worldScene, int x, int y) {
	struct color output = {0.0f, 0.0f, 0.0f, 0.0f};
	output.red =   mainRenderer.renderBuffer[(x + (worldScene->camera->height - y)*worldScene->camera->width)*3 + 0];
	output.green = mainRenderer.renderBuffer[(x + (worldScene->camera->height - y)*worldScene->camera->width)*3 + 1];
	output.blue =  mainRenderer.renderBuffer[(x + (worldScene->camera->height - y)*worldScene->camera->width)*3 + 2];
	output.alpha = 1.0f;
	return output;
}

//Compute view direction transforms
void transformCameraView(struct vector *direction) {
	for (int i = 1; i < mainRenderer.worldScene->camTransformCount; i++) {
		transformVector(direction, &mainRenderer.worldScene->camTransforms[i]);
		direction->isTransformed = false;
	}
}

void printRunningAverage(const time_t avgTime, int remainingTileCount) {
	time_t remainingTime = remainingTileCount * avgTime;
	//First print avg tile time
	printf("Avg tile time is: %li min (%li sec)", avgTime / 60, avgTime);
	printf(", render time remaining: %li min (%li sec)\n", remainingTime / 60, remainingTime);
}

void computeTimeAverage(struct renderTile tile) {
	mainRenderer.avgTileTime = mainRenderer.avgTileTime * (mainRenderer.timeSampleCount - 1);
	mainRenderer.avgTileTime += difftime(tile.stop, tile.start);
	mainRenderer.avgTileTime = mainRenderer.avgTileTime / mainRenderer.timeSampleCount;
	mainRenderer.timeSampleCount++;
	int remainingTileCount = mainRenderer.tileCount - mainRenderer.renderedTileCount;
	printRunningAverage(mainRenderer.avgTileTime, remainingTileCount);
}

/**
 A render thread
 
 @param arg Thread information (see threadInfo struct)
 @return Exits when thread is done
 */
#ifdef WINDOWS
DWORD WINAPI renderThread(LPVOID arg) {
#else
	void *renderThread(void *arg) {
#endif
		struct lightRay incidentRay;
		int x,y;
		bool first = true;
		struct threadInfo *tinfo = (struct threadInfo*)arg;
		
		//First time setup for each thread
		struct renderTile tile = getTile();
		time(&tile.start);
		
		while (!renderTilesEmpty()) {
			x = 0; y = 0;
			
			if (first) {
				//This is the first round, don't stop a previous tile
				first = false;
			} else {
				//Not first tile, deal with accordingly
				//Stop current tile
				mainRenderer.renderTiles[tile.tileNum].isRendering = false;
				time(&tile.stop);
				computeTimeAverage(tile);
				
				tile = getTile();
				time(&tile.start);
			}
			
			printf("Started tile %i/%i\r", mainRenderer.renderedTileCount, mainRenderer.tileCount);
			while (tile.completedSamples < mainRenderer.worldScene->camera->sampleCount+1 && mainRenderer.isRendering) {
				for (y = tile.endY; y > tile.startY; y--) {
					for (x = tile.startX; x < tile.endX; x++) {
						struct color output = getPixel(mainRenderer.worldScene, x, y);
						struct color sample = {0.0f,0.0f,0.0f,0.0f};
						
						int height = mainRenderer.worldScene->camera->height;
						int width = mainRenderer.worldScene->camera->width;
						
						double focalLength = 0.0f;
						if (mainRenderer.worldScene->camera->FOV > 0.0f
							&& mainRenderer.worldScene->camera->FOV < 189.0f) {
							focalLength = 0.5f * mainRenderer.worldScene->camera->width / tanf((double)(PIOVER180) * 0.5f * mainRenderer.worldScene->camera->FOV);
						}
						
						struct vector direction = {(x - 0.5f * mainRenderer.worldScene->camera->width) / focalLength,
							(y - 0.5f * mainRenderer.worldScene->camera->height) / focalLength, 1.0f};
						
						direction = normalizeVector(&direction);
						struct vector startPos = mainRenderer.worldScene->camera->pos;
						
						//And now compute transforms for position
						transformVector(&startPos, &mainRenderer.worldScene->camTransforms[0]);
						//...and compute rotation transforms for camera orientation
						transformCameraView(&direction);
						//Easy!
						
						//Set up ray
						incidentRay.start = startPos;
						incidentRay.direction = direction;
						incidentRay.rayType = rayTypeIncident;
						//Get sample
						sample = rayTrace(&incidentRay, mainRenderer.worldScene);
						
						//And process the running average
						output.red = output.red * (tile.completedSamples - 1);
						output.green = output.green * (tile.completedSamples - 1);
						output.blue = output.blue * (tile.completedSamples - 1);
						
						output = addColors(&output, &sample);
						
						output.red = output.red / tile.completedSamples;
						output.green = output.green / tile.completedSamples;
						output.blue = output.blue / tile.completedSamples;
						
						//Store render buffer
						mainRenderer.renderBuffer[(x + (height - y)*width)*3 + 0] = output.red;
						mainRenderer.renderBuffer[(x + (height - y)*width)*3 + 1] = output.green;
						mainRenderer.renderBuffer[(x + (height - y)*width)*3 + 2] = output.blue;
						
						//And store the image data
						mainRenderer.worldScene->camera->imgData[(x + (height - y)*width)*3 + 0] = (unsigned char)min(  output.red*255.0f, 255.0f);
						mainRenderer.worldScene->camera->imgData[(x + (height - y)*width)*3 + 1] = (unsigned char)min(output.green*255.0f, 255.0f);
						mainRenderer.worldScene->camera->imgData[(x + (height - y)*width)*3 + 2] = (unsigned char)min( output.blue*255.0f, 255.0f);
					}
				}
				tile.completedSamples++;
			}
			
		}
		mainRenderer.renderTiles[tile.tileNum].isRendering = false;
		printf("Thread %i done\n", tinfo->thread_num);
		tinfo->threadComplete = true;
#ifdef WINDOWS
		//Return possible codes here
		return 0;
#else
		pthread_exit((void*) arg);
#endif
	}