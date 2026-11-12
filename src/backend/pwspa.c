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

#include "pwspa.h"

#include <string.h>

#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/pod/iter.h>
#include <spa/param/param.h>
#include <spa/param/props.h>
#include <spa/param/route.h>
#include <spa/param/profile.h>
#include <spa/param/audio/raw.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/type-info.h>
#include <spa/debug/types.h>

uint32_t tm_param_props(void)        { return SPA_PARAM_Props; }
uint32_t tm_param_enum_profile(void) { return SPA_PARAM_EnumProfile; }
uint32_t tm_param_profile(void)      { return SPA_PARAM_Profile; }
uint32_t tm_param_enum_route(void)   { return SPA_PARAM_EnumRoute; }
uint32_t tm_param_route(void)        { return SPA_PARAM_Route; }
uint32_t tm_param_enum_format(void)  { return SPA_PARAM_EnumFormat; }

int tm_parse_props(const struct spa_pod *pod, struct tm_volume *out)
{
	const struct spa_pod_object *obj;
	const struct spa_pod_prop *prop;

	memset(out, 0, sizeof(*out));
	if (!pod || !spa_pod_is_object_type(pod, SPA_TYPE_OBJECT_Props))
		return 0;
	obj = (const struct spa_pod_object *)pod;

	SPA_POD_OBJECT_FOREACH(obj, prop) {
		switch (prop->key) {
		case SPA_PROP_channelVolumes:
			out->n_volumes = spa_pod_copy_array(&prop->value, SPA_TYPE_Float,
			                                    out->volumes, TM_MAX_CHANNELS);
			out->has_volumes = out->n_volumes > 0;
			break;
		case SPA_PROP_mute: {
			bool m;
			if (spa_pod_get_bool(&prop->value, &m) == 0) {
				out->mute = m;
				out->has_mute = 1;
			}
			break;
		}
		case SPA_PROP_channelMap:
			out->n_positions = spa_pod_copy_array(&prop->value, SPA_TYPE_Id,
			                                      out->positions, TM_MAX_CHANNELS);
			break;
		default:
			break;
		}
	}
	return out->has_volumes || out->has_mute;
}

int tm_parse_route(const struct spa_pod *pod, struct tm_route *out)
{
	const struct spa_pod *devices = NULL, *props = NULL;

	memset(out, 0, sizeof(*out));
	out->device = -1;
	out->description = NULL;
	if (!pod || !spa_pod_is_object_type(pod, SPA_TYPE_OBJECT_ParamRoute))
		return 0;
	if (spa_pod_parse_object(pod, SPA_TYPE_OBJECT_ParamRoute, NULL,
	        SPA_PARAM_ROUTE_index,       SPA_POD_Int(&out->index),
	        SPA_PARAM_ROUTE_direction,   SPA_POD_Id(&out->direction),
	        SPA_PARAM_ROUTE_name,        SPA_POD_String(&out->name),
	        SPA_PARAM_ROUTE_description, SPA_POD_OPT_String(&out->description),
	        SPA_PARAM_ROUTE_available,   SPA_POD_OPT_Id(&out->available),
	        SPA_PARAM_ROUTE_device,      SPA_POD_OPT_Int(&out->device),
	        SPA_PARAM_ROUTE_devices,     SPA_POD_OPT_Pod(&devices),
	        SPA_PARAM_ROUTE_props,       SPA_POD_OPT_Pod(&props)) < 0)
		return 0;
	if (devices)
		out->n_devices = spa_pod_copy_array(devices, SPA_TYPE_Int, out->devices, TM_MAX_ROUTE_DEVICES);
	if (props)
		tm_parse_props(props, &out->props);
	return 1;
}

int tm_parse_profile(const struct spa_pod *pod, struct tm_profile *out)
{
	memset(out, 0, sizeof(*out));
	if (!pod || !spa_pod_is_object_type(pod, SPA_TYPE_OBJECT_ParamProfile))
		return 0;
	if (spa_pod_parse_object(pod, SPA_TYPE_OBJECT_ParamProfile, NULL,
	        SPA_PARAM_PROFILE_index,       SPA_POD_Int(&out->index),
	        SPA_PARAM_PROFILE_name,        SPA_POD_OPT_String(&out->name),
	        SPA_PARAM_PROFILE_description, SPA_POD_OPT_String(&out->description),
	        SPA_PARAM_PROFILE_priority,    SPA_POD_OPT_Int(&out->priority),
	        SPA_PARAM_PROFILE_available,   SPA_POD_OPT_Id(&out->available)) < 0)
		return 0;
	return 1;
}

static void add_props(struct spa_pod_builder *b, const float *volumes, uint32_t n, int mute)
{
	struct spa_pod_frame f;
	spa_pod_builder_push_object(b, &f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
	if (volumes && n > 0) {
		spa_pod_builder_prop(b, SPA_PROP_channelVolumes, 0);
		spa_pod_builder_array(b, sizeof(float), SPA_TYPE_Float, n, volumes);
	}
	if (mute >= 0) {
		spa_pod_builder_prop(b, SPA_PROP_mute, 0);
		spa_pod_builder_bool(b, mute ? true : false);
	}
	spa_pod_builder_pop(b, &f);
}

const struct spa_pod *tm_build_props(void *buf, size_t size,
                                     const float *volumes, uint32_t n, int mute)
{
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, (uint32_t)size);
	add_props(&b, volumes, n, mute);
	if (b.state.offset > size)
		return NULL;
	return (const struct spa_pod *)buf;
}

const struct spa_pod *tm_build_route(void *buf, size_t size, int32_t index, int32_t device,
                                     const float *volumes, uint32_t n, int mute, int with_props)
{
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, (uint32_t)size);
	struct spa_pod_frame f;
	spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_Route);
	spa_pod_builder_prop(&b, SPA_PARAM_ROUTE_index, 0);
	spa_pod_builder_int(&b, index);
	spa_pod_builder_prop(&b, SPA_PARAM_ROUTE_device, 0);
	spa_pod_builder_int(&b, device);
	if (with_props) {
		spa_pod_builder_prop(&b, SPA_PARAM_ROUTE_props, 0);
		add_props(&b, volumes, n, mute);
	}
	spa_pod_builder_prop(&b, SPA_PARAM_ROUTE_save, 0);
	spa_pod_builder_bool(&b, true);
	spa_pod_builder_pop(&b, &f);
	if (b.state.offset > size)
		return NULL;
	return (const struct spa_pod *)buf;
}

const struct spa_pod *tm_build_profile(void *buf, size_t size, int32_t index)
{
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, (uint32_t)size);
	struct spa_pod_frame f;
	spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_ParamProfile, SPA_PARAM_Profile);
	spa_pod_builder_prop(&b, SPA_PARAM_PROFILE_index, 0);
	spa_pod_builder_int(&b, index);
	spa_pod_builder_prop(&b, SPA_PARAM_PROFILE_save, 0);
	spa_pod_builder_bool(&b, true);
	spa_pod_builder_pop(&b, &f);
	if (b.state.offset > size)
		return NULL;
	return (const struct spa_pod *)buf;
}

const struct spa_pod *tm_build_peak_format(void *buf, size_t size)
{
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, (uint32_t)size);
	struct spa_audio_info_raw info;
	memset(&info, 0, sizeof(info));
	info.format = SPA_AUDIO_FORMAT_F32;
	info.channels = 1;
	info.position[0] = SPA_AUDIO_CHANNEL_MONO;
	return spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);
}

const char *tm_channel_name(uint32_t position)
{
	const char *n = spa_debug_type_find_short_name(spa_type_audio_channel, position);
	return n ? n : "";
}
