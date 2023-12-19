//
//  worker.c
//  c-ray
//
//  Created by Valtteri Koskivuori on 06/04/2021.
//  Copyright © 2021-2023 Valtteri Koskivuori. All rights reserved.
//

#include "../../common/logging.h"
//Windows is annoying, so it's just not going to have networking. Because it is annoying and proprietary.
#ifndef WINDOWS
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "worker.h"
#include "protocol.h"

#include "../renderer/renderer.h"
#include "../renderer/pathtrace.h"
#include "../datatypes/tile.h"
#include "../datatypes/scene.h"
#include "../datatypes/camera.h"
#include "../../common/texture.h"
#include "../../common/platform/mutex.h"
#include "../../common/platform/thread.h"
#include "../../common/networking.h"
#include "../../common/string.h"
#include "../../common/gitsha1.h"
#include "../../common/timer.h"
#include "../../common/platform/signal.h"
#include "../accelerators/bvh.h"
#include <stdio.h>
#include <inttypes.h>

struct renderer *g_worker_renderer = NULL;
struct cr_mutex *g_worker_socket_mutex = NULL;
static bool g_running = false;

struct command workerCommands[] = {
	{"handshake", 0},
	{"loadScene", 1},
	{"startRender", 3},
};

struct workerThreadState {
	int thread_num;
	int connectionSocket;
	struct cr_mutex *socketMutex;
	struct camera *cam;
	struct renderer *renderer;
	bool threadComplete;
	size_t completedSamples;
	long avgSampleTime;
	struct render_tile *current;
	struct tile_set *tiles;
};

static cJSON *validateHandshake(cJSON *in) {
	const cJSON *version = cJSON_GetObjectItem(in, "version");
	const cJSON *githash = cJSON_GetObjectItem(in, "githash");
	if (!stringEquals(version->valuestring, PROTO_VERSION)) return errorResponse("Protocol version mismatch");
	if (!stringEquals(githash->valuestring, gitHash())) return errorResponse("Git hash mismatch");
	return newAction("startSync");
}

static cJSON *receiveScene(const cJSON *json) {
	
	// And then the scene
	logr(info, "Received scene description\n");
	g_worker_renderer = deserialize_renderer(cJSON_GetStringValue(cJSON_GetObjectItem(json, "data")));
	g_worker_socket_mutex = mutex_create();
	cJSON *resp = newAction("ready");
	
	// Stash in our capabilities here
	//TODO: Maybe some performance value in here, so the master knows how much work to assign?
	// For now just report back how many threads we've got available.
	cJSON_AddNumberToObject(resp, "threadCount", g_worker_renderer->prefs.threads);
	return resp;
}

// Tilenum of -1 communicates that it failed to get work, signaling the work thread to exit
static struct render_tile *getWork(int connectionSocket, struct tile_set *tiles) {
	if (!sendJSON(connectionSocket, newAction("getWork"), NULL)) {
		return NULL;
	}
	cJSON *response = readJSON(connectionSocket);
	if (!response) return NULL;
	cJSON *action = cJSON_GetObjectItem(response, "action");
	if (cJSON_IsString(action)) {
		if (stringEquals(action->valuestring, "renderComplete")) {
			logr(debug, "Master reported render is complete\n");
			cJSON_Delete(response);
			return NULL;
		}
	}
	// FIXME: Pass the tile index only, and rename it to index too.
	// In fact, this whole tile object thing might be a bit pointless, since
	// we can just keep track of indices, and compute the tile dims
	cJSON *tileJson = cJSON_GetObjectItem(response, "tile");
	struct render_tile tile = decodeTile(tileJson);
	tiles->tiles.items[tile.index] = tile;
	cJSON_Delete(response);
	return &tiles->tiles.items[tile.index];
}

static bool submitWork(int sock, struct texture *work, struct render_tile *forTile) {
	cJSON *result = serialize_texture(work);
	cJSON *tile = encodeTile(forTile);
	cJSON *package = newAction("submitWork");
	cJSON_AddItemToObject(package, "result", result);
	cJSON_AddItemToObject(package, "tile", tile);
	return sendJSON(sock, package, NULL);
}

static void *workerThread(void *arg) {
	block_signals();
	struct workerThreadState *thread = (struct workerThreadState *)thread_user_data(arg);
	struct renderer *r = thread->renderer;
	int sock = thread->connectionSocket;
	struct cr_mutex *sockMutex = thread->socketMutex;
	
	//Fetch initial task
	mutex_lock(sockMutex);
	thread->current = getWork(sock, thread->tiles);
	mutex_release(sockMutex);
	sampler *sampler = newSampler();

	struct camera *cam = thread->cam;
	
	struct timeval timer = { 0 };
	thread->completedSamples = 1;
	
	struct texture *tileBuffer = NULL;
	while (thread->current && r->state.rendering) {
		if (!tileBuffer || tileBuffer->width != thread->current->width || tileBuffer->height != thread->current->height) {
			destroyTexture(tileBuffer);
			tileBuffer = newTexture(float_p, thread->current->width, thread->current->height, 3);
		}
		long totalUsec = 0;
		long samples = 0;
		
		while (thread->completedSamples < r->prefs.sampleCount+1 && r->state.rendering) {
			timer_start(&timer);
			for (int y = thread->current->end.y - 1; y > thread->current->begin.y - 1; --y) {
				for (int x = thread->current->begin.x; x < thread->current->end.x; ++x) {
					if (r->state.render_aborted || !g_running) goto bail;
					uint32_t pixIdx = (uint32_t)(y * cam->width + x);
					initSampler(sampler, SAMPLING_STRATEGY, thread->completedSamples - 1, r->prefs.sampleCount, pixIdx);
					
					int local_x = x - thread->current->begin.x;
					int local_y = y - thread->current->begin.y;
					struct color output = textureGetPixel(tileBuffer, local_x, local_y, false);
					struct color sample = path_trace(cam_get_ray(cam, x, y, sampler), r->scene, r->prefs.bounces, sampler);

					nan_clamp(&sample, &output);
					
					//And process the running average
					output = colorCoef((float)(thread->completedSamples - 1), output);
					output = colorAdd(output, sample);
					float t = 1.0f / thread->completedSamples;
					output = colorCoef(t, output);
					
					setPixel(tileBuffer, output, local_x, local_y);
				}
			}
			//For performance metrics
			samples++;
			totalUsec += timer_get_us(timer);
			thread->completedSamples++;
			thread->current->completed_samples++;
			thread->avgSampleTime = totalUsec / samples;
		}
		
		thread->current->state = finished;
		mutex_lock(sockMutex);
		if (!submitWork(sock, tileBuffer, thread->current)) {
			mutex_release(sockMutex);
			break;
		}
		cJSON *resp = readJSON(sock);
		if (!resp || !stringEquals(cJSON_GetObjectItem(resp, "action")->valuestring, "ok")) {
			mutex_release(sockMutex);
			cJSON_Delete(resp);
			break;
		}
		cJSON_Delete(resp);
		mutex_release(sockMutex);
		thread->completedSamples = 1;
		mutex_lock(sockMutex);
		thread->current = getWork(sock, thread->tiles);
		mutex_release(sockMutex);
		tex_clear(tileBuffer);
	}
bail:
	destroySampler(sampler);
	destroyTexture(tileBuffer);
	
	thread->threadComplete = true;
	return 0;
}

#define active_msec  16

static cJSON *startRender(int connectionSocket, size_t thread_limit) {
	g_worker_renderer->state.rendering = true;
	g_worker_renderer->state.render_aborted = false;
	logr(info, "Starting network render job\n");
	
	size_t threadCount = thread_limit ? thread_limit : g_worker_renderer->prefs.threads;
	struct cr_thread *worker_threads = calloc(threadCount, sizeof(*worker_threads));
	struct workerThreadState *workerThreadStates = calloc(threadCount, sizeof(*workerThreadStates));
	
	struct camera selected_cam = g_worker_renderer->scene->cameras.items[g_worker_renderer->prefs.selected_camera];
	if (g_worker_renderer->prefs.override_width && g_worker_renderer->prefs.override_height) {
		selected_cam.width = g_worker_renderer->prefs.override_width ? (int)g_worker_renderer->prefs.override_width : selected_cam.width;
		selected_cam.height = g_worker_renderer->prefs.override_height ? (int)g_worker_renderer->prefs.override_height : selected_cam.height;
		cam_recompute_optics(&selected_cam);
	}
	struct renderer *r = g_worker_renderer;
	logr(info, "Got job: %s%i%s x %s%i%s, %s%zu%s samples with %s%zu%s bounces\n", KWHT, selected_cam.width, KNRM, KWHT, selected_cam.height, KNRM, KBLU, r->prefs.sampleCount, KNRM, KGRN, r->prefs.bounces, KNRM);
	logr(info, "Rendering with %s%zu%s local thread%s.\n",
		KRED,
		r->prefs.threads,
		KNRM,
		PLURAL(r->prefs.threads));
	//Quantize image into renderTiles
	struct tile_set set = tile_quantize(selected_cam.width, selected_cam.height, r->prefs.tileWidth, r->prefs.tileHeight, r->prefs.tileOrder);

	logr(info, "%u x %u tiles\n", r->prefs.tileWidth, r->prefs.tileHeight);
	// Do some pre-render preparations
	// Compute BVH acceleration structures for all meshes in the scene
	compute_accels(r->scene->meshes);

	// And then compute a single top-level BVH that contains all the objects
	logr(info, "Computing top-level BVH: ");
	struct timeval timer = {0};
	timer_start(&timer);
	r->scene->topLevel = build_top_level_bvh(r->scene->instances);
	printSmartTime(timer_get_ms(timer));
	logr(plain, "\n");

	for (size_t i = 0; i < set.tiles.count; ++i)
		set.tiles.items[i].total_samples = r->prefs.sampleCount;

	//Print a useful warning to user if the defined tile size results in less renderThreads
	if (set.tiles.count < r->prefs.threads) {
		logr(warning, "WARNING: Rendering with a less than optimal thread count due to large tile size!\n");
		logr(warning, "Reducing thread count from %zu to %zu\n", r->prefs.threads, set.tiles.count);
		r->prefs.threads = set.tiles.count;
	}

	//Create render threads (Nonblocking)
	for (size_t t = 0; t < threadCount; ++t) {
		workerThreadStates[t] = (struct workerThreadState){
				.thread_num = t,
				.connectionSocket = connectionSocket,
				.socketMutex = g_worker_socket_mutex,
				.renderer = g_worker_renderer,
				.tiles = &set,
				.cam = &selected_cam};
		worker_threads[t] = (struct cr_thread){.thread_fn = workerThread, .user_data = &workerThreadStates[t]};
		if (thread_start(&worker_threads[t]))
			logr(error, "Failed to create a crThread.\n");
	}
	
	int pauser = 0;
	while (g_worker_renderer->state.rendering) {
		// Send stats about 4x/s
		if (pauser == 256 / active_msec) {
			cJSON *stats = newAction("stats");
			cJSON *array = cJSON_AddArrayToObject(stats, "tiles");
			logr(plain, "\33[2K\r");
			logr(debug, "( ");
			for (size_t t = 0; t < threadCount; ++t) {
				struct render_tile *tile = workerThreadStates[t].current;
				if (tile) {
					cJSON_AddItemToArray(array, encodeTile(tile));
					logr(plain, "%i: %5zu%s", tile->index, tile->completed_samples, t < threadCount - 1 ? ", " : " ");
				}
			}
			logr(plain, ")");

			mutex_lock(g_worker_socket_mutex);
			if (!sendJSON(connectionSocket, stats, NULL)) {
				logr(debug, "Connection lost, bailing out.\n");
				// Setting this flag also kills the threads.
				g_worker_renderer->state.rendering = false;
			}
			mutex_release(g_worker_socket_mutex);
			pauser = 0;
		}
		pauser++;

		size_t inactive = 0;
		for (size_t t = 0; t < threadCount; ++t) {
			if (workerThreadStates[t].threadComplete) inactive++;
		}
		if (g_worker_renderer->state.render_aborted || inactive == threadCount)
			g_worker_renderer->state.rendering = false;

		timer_sleep_ms(active_msec);
	}

	//Make sure workder threads are terminated before continuing (This blocks)
	for (size_t t = 0; t < threadCount; ++t) {
		thread_wait(&worker_threads[t]);
	}
	tile_set_free(&set);
	free(worker_threads);
	free(workerThreadStates);
	return NULL;
}

// Worker command handler
static cJSON *processCommand(int connectionSocket, cJSON *json, size_t thread_limit) {
	if (!json) {
		return errorResponse("Couldn't parse incoming JSON");
	}
	const cJSON *action = cJSON_GetObjectItem(json, "action");
	if (!cJSON_IsString(action)) {
		return errorResponse("No action provided");
	}
	
	switch (matchCommand(workerCommands, sizeof(workerCommands) / sizeof(struct command), action->valuestring)) {
		case 0:
			return validateHandshake(json);
			break;
		case 1:
			return receiveScene(json);
			break;
		case 3:
			// startRender contains worker event loop and blocks until render completion.
			return startRender(connectionSocket, thread_limit);
			break;
		default:
			return errorResponse("Unknown command");
			break;
	}
	
	ASSERT_NOT_REACHED();
}

static void workerCleanup() {
	if (!g_worker_renderer) return;
	renderer_destroy(g_worker_renderer);
	g_worker_renderer = NULL;
}

bool isShutdown(cJSON *json) {
	cJSON *action = cJSON_GetObjectItem(json, "action");
	if (cJSON_IsString(action)) {
		if (stringEquals(action->valuestring, "shutdown")) {
			return true;
		}
	}
	return false;
}

// Hack. Yoink the receiving socket fd to close it. Don't see another way.
static int recvsock_temp = 0;
void exitHandler(int sig) {
	(void)sig;
	ASSERT(sig == SIGINT);
	printf("\n");
	logr(info, "Received ^C, shutting down worker.\n");
	g_running = false;
	close(recvsock_temp);
}

int worker_start(int port, size_t thread_limit) {
	signal(SIGPIPE, SIG_IGN);
	if (registerHandler(sigint, exitHandler) < 0) {
		logr(error, "registerHandler failed\n");
	}
	int receivingSocket, connectionSocket;
	struct sockaddr_in ownAddress;
	receivingSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (receivingSocket == -1) {
		logr(error, "Socket creation failed.\n");
	}
	
	bzero(&ownAddress, sizeof(ownAddress));
	ownAddress.sin_family = AF_INET;
	ownAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	ownAddress.sin_port = htons(port);
	
	int opt_val = 1;
	setsockopt(receivingSocket, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val));
	
	if ((bind(receivingSocket, (struct sockaddr *)&ownAddress, sizeof(ownAddress))) != 0) {
		logr(error, "Failed to bind to socket\n");
	}
	
	recvsock_temp = receivingSocket;
	if (listen(receivingSocket, 1) != 0) {
		logr(error, "It wouldn't listen\n");
	}
	
	struct sockaddr_in masterAddress;
	socklen_t len = sizeof(masterAddress);
	char *buf = NULL;
	
	g_running = true;
	
	while (g_running) {
		logr(info, "Listening for connections on port %i\n", port);
		if (thread_limit) logr(info, "Limiting to %zu threads\n", thread_limit);
		connectionSocket = accept(receivingSocket, (struct sockaddr *)&masterAddress, &len);
		if (connectionSocket < 0) {
			logr(debug, "Failed to accept\n");
			goto bail;
		}
		logr(info, "Got connection from %s\n", inet_ntoa(masterAddress.sin_addr));
		
		for (;;) {
			buf = NULL;
			size_t length = 0;
			struct timeval timer;
			timer_start(&timer);
			ssize_t read = chunkedReceive(connectionSocket, &buf, &length);
			if (read == 0) break;
			if (read < 0) {
				logr(warning, "Something went wrong. Error: %s\n", strerror(errno));
				break;
			}
			cJSON *message = cJSON_Parse(buf);
			if (isShutdown(message)) {
				g_running = false;
				cJSON_Delete(message);
				break;
			}
			cJSON *myResponse = processCommand(connectionSocket, message, thread_limit);
			cJSON_Delete(message);
			if (!myResponse) {
				if (buf) {
					free(buf);
					buf = NULL;
				}
				break;
			}
			char *responseText = cJSON_PrintUnformatted(myResponse);
			if (!chunkedSend(connectionSocket, responseText, NULL)) {
				logr(debug, "chunkedSend() failed, error %s\n", strerror(errno));
				break;
			};
			free(responseText);
			if (buf) {
				free(buf);
				buf = NULL;
			}
			if (containsGoodbye(myResponse) || containsError(myResponse)) {
				cJSON_Delete(myResponse);
				break;
			}
			cJSON_Delete(myResponse);
		}
	bail:
		if (g_running) {
			logr(info, "Cleaning up for next render\n");
		} else {
			logr(info, "Received shutdown command, exiting\n");
		}
		shutdown(connectionSocket, SHUT_RDWR);
		close(connectionSocket);
		workerCleanup(); // Prepare for next render
	}
	if (buf) free(buf);
	shutdown(receivingSocket, SHUT_RDWR);
	close(receivingSocket);
	return 0;
}
#else
int worker_start(int port, size_t thread_limit) {
	(void)port;
	(void)thread_limit;
	logr(error, "c-ray doesn't support the proprietary networking stack on Windows yet. Sorry!\n");
}
#endif
