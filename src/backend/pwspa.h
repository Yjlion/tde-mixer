/*
 * tde-mixer - per-application volume mixer for the Trinity Desktop Environment
 *
 * Copyright (C) 2026 tde-mixer developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef PWSPA_H
#define PWSPA_H

/*
 * SPA POD building and parsing for the PipeWire backend. This is plain C:
 * the SPA_POD_* convenience macros use C99 compound literals, which C++
 * compilers do not accept everywhere (and not at all with -pedantic).
 * Strings returned in the structures point into the POD and are only valid
 * while it is.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct spa_pod;

#define TM_MAX_CHANNELS 64
#define TM_MAX_ROUTE_DEVICES 32

struct tm_volume {
	int has_volumes;
	int has_mute;
	uint32_t n_volumes;
	float volumes[TM_MAX_CHANNELS];      /* linear, as PipeWire stores them */
	uint32_t n_positions;
	uint32_t positions[TM_MAX_CHANNELS]; /* enum spa_audio_channel */
	int mute;
};

struct tm_route {
	int32_t index;
	uint32_t direction;                  /* enum spa_direction */
	const char *name;
	const char *description;
	uint32_t available;                  /* enum spa_param_availability */
	int32_t device;                      /* -1 unless an active Route */
	uint32_t n_devices;
	int32_t devices[TM_MAX_ROUTE_DEVICES];
	struct tm_volume props;              /* only with an active Route */
};

struct tm_profile {
	int32_t index;
	const char *name;
	const char *description;
	uint32_t available;
	uint32_t priority;
};

enum { TM_DIRECTION_INPUT = 0, TM_DIRECTION_OUTPUT = 1 };
enum { TM_AVAILABLE_UNKNOWN = 0, TM_AVAILABLE_NO = 1, TM_AVAILABLE_YES = 2 };

/* the SPA_PARAM_* ids used by the backend */
uint32_t tm_param_props(void);
uint32_t tm_param_enum_profile(void);
uint32_t tm_param_profile(void);
uint32_t tm_param_enum_route(void);
uint32_t tm_param_route(void);
uint32_t tm_param_enum_format(void);

/* parsing: return non-zero on success */
int tm_parse_props(const struct spa_pod *pod, struct tm_volume *out);
int tm_parse_route(const struct spa_pod *pod, struct tm_route *out);
int tm_parse_profile(const struct spa_pod *pod, struct tm_profile *out);

/* building: return the POD inside buf, or NULL if buf is too small.
 * volumes == NULL leaves the volume alone, mute < 0 leaves mute alone */
const struct spa_pod *tm_build_props(void *buf, size_t size,
                                     const float *volumes, uint32_t n, int mute);
const struct spa_pod *tm_build_route(void *buf, size_t size, int32_t index, int32_t device,
                                     const float *volumes, uint32_t n, int mute, int with_props);
const struct spa_pod *tm_build_profile(void *buf, size_t size, int32_t index);
/* F32, mono, any rate: the format of the level meter streams */
const struct spa_pod *tm_build_peak_format(void *buf, size_t size);

/* short channel name ("FL", "FR", ...) */
const char *tm_channel_name(uint32_t position);

#ifdef __cplusplus
}
#endif

#endif
