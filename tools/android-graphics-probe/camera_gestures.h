#ifndef CAMERA_GESTURES_H
#define CAMERA_GESTURES_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct { float x, y; } TouchPoint;
typedef struct { int64_t id; TouchPoint point; } CameraFinger;
typedef struct {
	CameraFinger fingers[2];
	unsigned count;
	bool canTap;
	TouchPoint start;
	TouchPoint pan;
	float zoom;
} CameraGesture;

static void CameraTouchDown(CameraGesture* camera, int64_t id, TouchPoint point)
{
	if (camera->count == 0) {
		camera->start = point;
		camera->canTap = true;
	} else {
		camera->canTap = false;
	}
	if (camera->count < 2)
		camera->fingers[camera->count++] = (CameraFinger){id, point};
}

static void CameraTouchMove(CameraGesture* camera, int64_t id, TouchPoint point, float aspect)
{
	for (unsigned i = 0; i < camera->count; ++i) {
		if (camera->fingers[i].id != id)
			continue;
		TouchPoint previous = camera->fingers[i].point;
		camera->fingers[i].point = point;
		if (camera->count == 2) {
			TouchPoint other = camera->fingers[1 - i].point;
			float before = hypotf((previous.x - other.x) * aspect, previous.y - other.y);
			float after = hypotf((point.x - other.x) * aspect, point.y - other.y);
			float nextZoom = camera->zoom;
			if (before > 0.005f)
				nextZoom = fminf(2.8f, fmaxf(0.65f, camera->zoom * after / before));
			float ratio = nextZoom / camera->zoom;
			float oldX = previous.x + other.x - 1;
			float oldY = 1 - previous.y - other.y;
			camera->pan.x = point.x + other.x - 1 + (camera->pan.x - oldX) * ratio;
			camera->pan.y = 1 - point.y - other.y + (camera->pan.y - oldY) * ratio;
			camera->zoom = nextZoom;
		} else {
			if (camera->canTap) {
				float moved = hypotf((point.x - camera->start.x) * aspect, point.y - camera->start.y);
				if (moved < 0.018f)
					return;
				camera->canTap = false;
				previous = camera->start;
			}
			camera->pan.x += 2 * (point.x - previous.x);
			camera->pan.y -= 2 * (point.y - previous.y);
		}
		return;
	}
}

static bool CameraTouchUp(CameraGesture* camera, int64_t id, TouchPoint point, float aspect)
{
	for (unsigned i = 0; i < camera->count; ++i) {
		if (camera->fingers[i].id != id)
			continue;
		CameraTouchMove(camera, id, point, aspect);
		bool tap = camera->count == 1 && camera->canTap;
		camera->fingers[i] = camera->fingers[--camera->count];
		camera->canTap = false;
		return tap;
	}
	return false;
}

#endif
