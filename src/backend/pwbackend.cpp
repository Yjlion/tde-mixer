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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* PipeWire first: TQt's "signals"/"slots"/"emit" macros must not reach it */
#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>
#include <spa/utils/result.h>

#include "pwbackend.h"
#include "pwspa.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include <tqtimer.h>

#define PEAK_PROPERTY "tde-mixer.peak"

namespace {

enum ObjKind { NodeObj, DeviceObj, MetadataObj, LinkObj };

struct RouteInfo {
	int32_t index;
	uint32_t direction;
	std::string name;
	std::string description;
	uint32_t available;
	int32_t device;
	std::vector<int32_t> devices;
	bool hasVolume;
	std::vector<float> linear;
	std::vector<uint32_t> positions;
	bool hasMute;
	bool mute;
	RouteInfo() : index(-1), direction(0), available(0), device(-1), hasVolume(false), hasMute(false), mute(false) {}
};

struct ProfileInfo {
	int32_t index;
	Audio::Profile profile;
};

struct PwObject {
	PipeWireBackend *backend;
	uint32_t id;
	ObjKind kind;
	pw_proxy *proxy;
	spa_hook listener;
	bool hooked;
	std::map<std::string, std::string> props;

	/* node */
	bool haveVolume;
	std::vector<float> linear;
	std::vector<uint32_t> positions;
	bool mute;
	bool running;

	/* device */
	std::vector<ProfileInfo> profiles;
	int32_t activeProfile;
	std::vector<RouteInfo> routes;        // EnumRoute
	std::vector<RouteInfo> activeRoutes;  // Route

	/* link */
	uint32_t outNode, inNode;

	PwObject() : backend(0), id(SPA_ID_INVALID), kind(NodeObj), proxy(0), hooked(false),
	             haveVolume(false), mute(false), running(false), activeProfile(-1),
	             outNode(SPA_ID_INVALID), inNode(SPA_ID_INVALID) {}

	std::string get(const char *key) const {
		std::map<std::string, std::string>::const_iterator it = props.find(key);
		return it == props.end() ? std::string() : it->second;
	}
};

struct PwPeak {
	PipeWireBackend *backend;
	uint32_t id;
	pw_stream *stream;
	spa_hook listener;
};

} // namespace

struct PipeWireBackend::Private {
	pw_thread_loop *loop;
	pw_context *context;
	pw_core *core;
	pw_registry *registry;
	spa_hook coreListener;
	spa_hook registryListener;
	bool failed;
	bool reconnectScheduled;
	int syncSeq;
	int syncRound;
	std::string version;
	std::map<uint32_t, PwObject *> objects;
	PwObject *metadata;
	std::string defaultSink, defaultSource;            // actual defaults
	std::string configuredSink, configuredSource;      // user choice
	std::map<uint32_t, PwPeak *> peaks;
	Private() : loop(0), context(0), core(0), registry(0), failed(false), reconnectScheduled(false),
	            syncSeq(0), syncRound(0), metadata(0) {}
};

/* ------------------------------------------------------------------ */
/* helpers                                                            */

static float toPerceptual(float linear)
{
	return linear <= 0.0f ? 0.0f : cbrtf(linear);
}

static float toLinear(float perceptual)
{
	return perceptual <= 0.0f ? 0.0f : perceptual * perceptual * perceptual;
}

static std::string jsonName(const char *value)
{
	/* { "name": "alsa_output.pci-0000_00_1f.3.analog-stereo" } */
	if (!value)
		return std::string();
	const char *p = strstr(value, "\"name\"");
	if (!p)
		return std::string();
	p = strchr(p + 6, ':');
	if (!p)
		return std::string();
	p = strchr(p, '"');
	if (!p)
		return std::string();
	++p;
	std::string out;
	while (*p && *p != '"') {
		if (*p == '\\' && p[1])
			++p;
		out += *p++;
	}
	return out;
}

static std::string jsonEscape(const std::string &s)
{
	std::string out;
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '"' || s[i] == '\\')
			out += '\\';
		out += s[i];
	}
	return out;
}

static void copyDict(std::map<std::string, std::string> &props, const struct spa_dict *dict)
{
	if (!dict)
		return;
	const struct spa_dict_item *item;
	spa_dict_for_each(item, dict)
		props[item->key ? item->key : ""] = item->value ? item->value : "";
}

static RouteInfo routeFrom(const struct tm_route &r)
{
	RouteInfo ri;
	ri.index = r.index;
	ri.direction = r.direction;
	ri.name = r.name ? r.name : "";
	ri.description = r.description ? r.description : ri.name;
	ri.available = r.available;
	ri.device = r.device;
	for (uint32_t i = 0; i < r.n_devices; ++i)
		ri.devices.push_back(r.devices[i]);
	ri.hasVolume = r.props.has_volumes;
	for (uint32_t i = 0; i < r.props.n_volumes; ++i)
		ri.linear.push_back(r.props.volumes[i]);
	for (uint32_t i = 0; i < r.props.n_positions; ++i)
		ri.positions.push_back(r.props.positions[i]);
	ri.hasMute = r.props.has_mute;
	ri.mute = r.props.mute;
	return ri;
}

static std::vector<std::string> channelNames(const std::vector<uint32_t> &positions, size_t n)
{
	std::vector<std::string> out;
	for (size_t i = 0; i < n; ++i)
		out.push_back(i < positions.size() ? tm_channel_name(positions[i]) : "");
	return out;
}

/* ------------------------------------------------------------------ */
/* the model, rebuilt from the objects after every change             */

static void rebuild(PipeWireBackend *b)
{
	PipeWireBackend::Private *d = b->d;
	Audio::Model &m = b->rawModel();
	bool connected = m.connected;
	std::string server = m.serverName;
	m.clear();
	m.connected = connected;
	m.serverName = server;
	if (!connected)
		return;

	m.defaultSink = d->defaultSink.empty() ? d->configuredSink : d->defaultSink;
	m.defaultSource = d->defaultSource.empty() ? d->configuredSource : d->defaultSource;

	std::map<uint32_t, PwObject *> &objs = d->objects;

	for (std::map<uint32_t, PwObject *>::iterator it = objs.begin(); it != objs.end(); ++it) {
		PwObject *o = it->second;
		if (o->kind == DeviceObj) {
			Audio::Card c;
			c.id = o->id;
			c.name = o->get(PW_KEY_DEVICE_NAME);
			c.description = o->get(PW_KEY_DEVICE_DESCRIPTION);
			if (c.description.empty())
				c.description = o->get(PW_KEY_DEVICE_NICK);
			if (c.description.empty())
				c.description = c.name;
			c.iconName = o->get(PW_KEY_DEVICE_ICON_NAME);
			for (size_t i = 0; i < o->profiles.size(); ++i) {
				c.profiles.push_back(o->profiles[i].profile);
				if (o->profiles[i].index == o->activeProfile)
					c.activeProfile = o->profiles[i].profile.name;
			}
			m.cards[c.id] = c;
			continue;
		}
		if (o->kind != NodeObj)
			continue;

		std::string mc = o->get(PW_KEY_MEDIA_CLASS);
		if (mc == "Audio/Sink" || mc == "Audio/Source" || mc == "Audio/Source/Virtual") {
			Audio::Device dev;
			dev.id = o->id;
			dev.kind = mc == "Audio/Sink" ? Audio::Sink : Audio::Source;
			dev.name = o->get(PW_KEY_NODE_NAME);
			dev.description = o->get(PW_KEY_NODE_DESCRIPTION);
			if (dev.description.empty())
				dev.description = o->get(PW_KEY_NODE_NICK);
			if (dev.description.empty())
				dev.description = dev.name;

			const PwObject *card = 0;
			std::string devId = o->get(PW_KEY_DEVICE_ID);
			if (!devId.empty()) {
				std::map<uint32_t, PwObject *>::iterator c = objs.find((uint32_t)atoi(devId.c_str()));
				if (c != objs.end() && c->second->kind == DeviceObj)
					card = c->second;
			}
			dev.isVirtual = card == 0;
			dev.cardId = card ? card->id : Audio::InvalidId;
			dev.iconName = o->get(PW_KEY_DEVICE_ICON_NAME);
			if (dev.iconName.empty() && card)
				dev.iconName = card->get(PW_KEY_DEVICE_ICON_NAME);
			if (dev.iconName.empty())
				dev.iconName = o->get("device.icon_name");

			/* volume: the active route of the card if there is one, else the node */
			const RouteInfo *route = 0;
			std::string cpd = o->get("card.profile.device");
			if (card && !cpd.empty()) {
				int32_t devIndex = atoi(cpd.c_str());
				uint32_t dir = dev.kind == Audio::Sink ? TM_DIRECTION_OUTPUT : TM_DIRECTION_INPUT;
				for (size_t i = 0; i < card->activeRoutes.size(); ++i)
					if (card->activeRoutes[i].device == devIndex && card->activeRoutes[i].direction == dir)
						route = &card->activeRoutes[i];
				for (size_t i = 0; i < card->routes.size(); ++i) {
					const RouteInfo &r = card->routes[i];
					if (r.direction != dir)
						continue;
					if (std::find(r.devices.begin(), r.devices.end(), devIndex) == r.devices.end())
						continue;
					Audio::Port p;
					p.name = r.name;
					p.description = r.description;
					p.available = r.available != TM_AVAILABLE_NO;
					dev.ports.push_back(p);
				}
				if (route)
					dev.activePort = route->name;
			}
			if (route && route->hasVolume) {
				for (size_t i = 0; i < route->linear.size(); ++i)
					dev.volumes.push_back(toPerceptual(route->linear[i]));
				dev.channels = channelNames(route->positions.empty() ? o->positions : route->positions,
				                            dev.volumes.size());
				dev.muted = route->hasMute ? route->mute : o->mute;
				dev.hasVolume = true;
			} else if (o->haveVolume) {
				for (size_t i = 0; i < o->linear.size(); ++i)
					dev.volumes.push_back(toPerceptual(o->linear[i]));
				dev.channels = channelNames(o->positions, dev.volumes.size());
				dev.muted = o->mute;
				dev.hasVolume = true;
			}
			m.devices[dev.id] = dev;
		} else if (mc == "Stream/Output/Audio" || mc == "Stream/Input/Audio") {
			if (o->props.count(PEAK_PROPERTY) ||
			    o->get(PW_KEY_APP_ID) == "org.PulseAudio.pavucontrol" ||
			    o->get(PW_KEY_STREAM_MONITOR) == "true")
				continue;
			Audio::Stream s;
			s.id = o->id;
			s.kind = mc == "Stream/Output/Audio" ? Audio::Playback : Audio::Recording;
			s.appName = o->get(PW_KEY_APP_NAME);
			if (s.appName.empty())
				s.appName = o->get(PW_KEY_NODE_DESCRIPTION);
			if (s.appName.empty())
				s.appName = o->get(PW_KEY_NODE_NAME);
			s.binary = o->get(PW_KEY_APP_PROCESS_BINARY);
			s.iconName = o->get(PW_KEY_APP_ICON_NAME);
			if (s.iconName.empty())
				s.iconName = o->get("application.icon_name");
			s.mediaName = o->get(PW_KEY_MEDIA_NAME);
			s.role = o->get(PW_KEY_MEDIA_ROLE);
			for (size_t i = 0; i < o->linear.size(); ++i)
				s.volumes.push_back(toPerceptual(o->linear[i]));
			s.channels = channelNames(o->positions, s.volumes.size());
			s.muted = o->mute;
			s.hasVolume = o->haveVolume;
			s.corked = !o->running;
			m.streams[s.id] = s;
		}
	}

	/* which device each stream is linked to */
	for (std::map<uint32_t, PwObject *>::iterator it = objs.begin(); it != objs.end(); ++it) {
		PwObject *o = it->second;
		if (o->kind != LinkObj)
			continue;
		std::map<uint32_t, Audio::Stream>::iterator s = m.streams.find(o->outNode);
		if (s != m.streams.end() && s->second.kind == Audio::Playback && m.devices.count(o->inNode))
			s->second.deviceId = o->inNode;
		s = m.streams.find(o->inNode);
		if (s != m.streams.end() && s->second.kind == Audio::Recording && m.devices.count(o->outNode))
			s->second.deviceId = o->outNode;
	}

	b->requestSync();
}

/* ------------------------------------------------------------------ */
/* object listeners (loop thread, lock held)                          */

static void nodeInfo(void *data, const struct pw_node_info *info)
{
	PwObject *o = (PwObject *)data;
	if (info->change_mask & PW_NODE_CHANGE_MASK_PROPS)
		copyDict(o->props, info->props);
	if (info->change_mask & PW_NODE_CHANGE_MASK_STATE)
		o->running = info->state == PW_NODE_STATE_RUNNING;
	rebuild(o->backend);
}

static void nodeParam(void *data, int, uint32_t id, uint32_t, uint32_t, const struct spa_pod *param)
{
	PwObject *o = (PwObject *)data;
	if (id != tm_param_props())
		return;
	struct tm_volume v;
	if (!tm_parse_props(param, &v))
		return;
	if (v.has_volumes) {
		o->linear.assign(v.volumes, v.volumes + v.n_volumes);
		o->haveVolume = true;
	}
	if (v.n_positions)
		o->positions.assign(v.positions, v.positions + v.n_positions);
	if (v.has_mute)
		o->mute = v.mute;
	rebuild(o->backend);
}

static void deviceInfo(void *data, const struct pw_device_info *info)
{
	PwObject *o = (PwObject *)data;
	if (info->change_mask & PW_DEVICE_CHANGE_MASK_PROPS)
		copyDict(o->props, info->props);
	rebuild(o->backend);
}

static void deviceParam(void *data, int, uint32_t id, uint32_t index, uint32_t, const struct spa_pod *param)
{
	PwObject *o = (PwObject *)data;
	if (id == tm_param_enum_profile()) {
		struct tm_profile p;
		if (index == 0)
			o->profiles.clear();
		if (!tm_parse_profile(param, &p))
			return;
		ProfileInfo pi;
		pi.index = p.index;
		pi.profile.name = p.name ? p.name : "";
		pi.profile.description = p.description ? p.description : pi.profile.name;
		pi.profile.available = p.available != TM_AVAILABLE_NO;
		pi.profile.priority = p.priority;
		for (size_t i = 0; i < o->profiles.size(); ++i)
			if (o->profiles[i].index == pi.index) {
				o->profiles[i] = pi;
				pi.index = -1;
			}
		if (pi.index >= 0)
			o->profiles.push_back(pi);
	} else if (id == tm_param_profile()) {
		struct tm_profile p;
		if (tm_parse_profile(param, &p))
			o->activeProfile = p.index;
	} else if (id == tm_param_enum_route()) {
		struct tm_route r;
		if (index == 0)
			o->routes.clear();
		if (!tm_parse_route(param, &r))
			return;
		o->routes.push_back(routeFrom(r));
	} else if (id == tm_param_route()) {
		struct tm_route r;
		if (index == 0)
			o->activeRoutes.clear();
		if (!tm_parse_route(param, &r))
			return;
		RouteInfo ri = routeFrom(r);
		for (size_t i = 0; i < o->activeRoutes.size(); ++i)
			if (o->activeRoutes[i].device == ri.device && o->activeRoutes[i].direction == ri.direction) {
				o->activeRoutes[i] = ri;
				ri.index = -2;
			}
		if (ri.index != -2)
			o->activeRoutes.push_back(ri);
	} else {
		return;
	}
	rebuild(o->backend);
}

static int metadataProperty(void *data, uint32_t subject, const char *key, const char *, const char *value)
{
	PwObject *o = (PwObject *)data;
	PipeWireBackend::Private *d = o->backend->d;
	if (subject != PW_ID_CORE)
		return 0;
	if (!key) {
		d->defaultSink.clear();
		d->defaultSource.clear();
		d->configuredSink.clear();
		d->configuredSource.clear();
	} else if (!strcmp(key, "default.audio.sink")) {
		d->defaultSink = jsonName(value);
	} else if (!strcmp(key, "default.audio.source")) {
		d->defaultSource = jsonName(value);
	} else if (!strcmp(key, "default.configured.audio.sink")) {
		d->configuredSink = jsonName(value);
	} else if (!strcmp(key, "default.configured.audio.source")) {
		d->configuredSource = jsonName(value);
	} else {
		return 0;
	}
	rebuild(o->backend);
	return 0;
}

static pw_node_events s_nodeEvents;
static pw_device_events s_deviceEvents;
static pw_metadata_events s_metadataEvents;
static pw_registry_events s_registryEvents;
static pw_core_events s_coreEvents;
static pw_stream_events s_peakEvents;

static void destroyObject(PwObject *o)
{
	if (o->hooked)
		spa_hook_remove(&o->listener);
	if (o->proxy)
		pw_proxy_destroy(o->proxy);
	delete o;
}

static void registryGlobal(void *data, uint32_t id, uint32_t, const char *type, uint32_t, const struct spa_dict *props)
{
	PipeWireBackend *b = (PipeWireBackend *)data;
	PipeWireBackend::Private *d = b->d;
	const char *mc = props ? spa_dict_lookup(props, PW_KEY_MEDIA_CLASS) : 0;

	PwObject *o = 0;
	if (!strcmp(type, PW_TYPE_INTERFACE_Node)) {
		if (!mc || (strcmp(mc, "Audio/Sink") && strcmp(mc, "Audio/Source") && strcmp(mc, "Audio/Source/Virtual") &&
		            strcmp(mc, "Stream/Output/Audio") && strcmp(mc, "Stream/Input/Audio")))
			return;
		o = new PwObject;
		o->kind = NodeObj;
		o->proxy = (pw_proxy *)pw_registry_bind(d->registry, id, type, PW_VERSION_NODE, 0);
		if (o->proxy) {
			pw_node_add_listener((pw_node *)o->proxy, &o->listener, &s_nodeEvents, o);
			o->hooked = true;
			uint32_t ids[1] = { tm_param_props() };
			pw_node_subscribe_params((pw_node *)o->proxy, ids, 1);
		}
	} else if (!strcmp(type, PW_TYPE_INTERFACE_Device)) {
		if (!mc || strcmp(mc, "Audio/Device"))
			return;
		o = new PwObject;
		o->kind = DeviceObj;
		o->proxy = (pw_proxy *)pw_registry_bind(d->registry, id, type, PW_VERSION_DEVICE, 0);
		if (o->proxy) {
			pw_device_add_listener((pw_device *)o->proxy, &o->listener, &s_deviceEvents, o);
			o->hooked = true;
			uint32_t ids[4] = { tm_param_enum_profile(), tm_param_profile(),
			                    tm_param_enum_route(), tm_param_route() };
			pw_device_subscribe_params((pw_device *)o->proxy, ids, 4);
		}
	} else if (!strcmp(type, PW_TYPE_INTERFACE_Metadata)) {
		const char *name = props ? spa_dict_lookup(props, PW_KEY_METADATA_NAME) : 0;
		if (!name || strcmp(name, "default") || d->metadata)
			return;
		o = new PwObject;
		o->kind = MetadataObj;
		o->proxy = (pw_proxy *)pw_registry_bind(d->registry, id, type, PW_VERSION_METADATA, 0);
		if (o->proxy) {
			pw_metadata_add_listener((pw_metadata *)o->proxy, &o->listener, &s_metadataEvents, o);
			o->hooked = true;
		}
		d->metadata = o;
	} else if (!strcmp(type, PW_TYPE_INTERFACE_Link)) {
		o = new PwObject;
		o->kind = LinkObj;
		const char *out = props ? spa_dict_lookup(props, PW_KEY_LINK_OUTPUT_NODE) : 0;
		const char *in = props ? spa_dict_lookup(props, PW_KEY_LINK_INPUT_NODE) : 0;
		o->outNode = out ? (uint32_t)atoi(out) : SPA_ID_INVALID;
		o->inNode = in ? (uint32_t)atoi(in) : SPA_ID_INVALID;
	} else {
		return;
	}

	o->backend = b;
	o->id = id;
	if (props)
		copyDict(o->props, props);
	d->objects[id] = o;
	rebuild(b);
}

static void registryGlobalRemove(void *data, uint32_t id)
{
	PipeWireBackend *b = (PipeWireBackend *)data;
	PipeWireBackend::Private *d = b->d;
	std::map<uint32_t, PwObject *>::iterator it = d->objects.find(id);
	if (it == d->objects.end())
		return;
	PwObject *o = it->second;
	d->objects.erase(it);
	if (o == d->metadata) {
		d->metadata = 0;
		d->defaultSink.clear();
		d->defaultSource.clear();
	}
	destroyObject(o);
	b->rawPeaks().erase(id);
	rebuild(b);
}

static void coreInfo(void *data, const struct pw_core_info *info)
{
	PipeWireBackend *b = (PipeWireBackend *)data;
	if (info && info->version)
		b->d->version = info->version;
	b->rawModel().serverName = std::string("PipeWire ") + b->d->version;
}

static void coreDone(void *data, uint32_t id, int seq)
{
	PipeWireBackend *b = (PipeWireBackend *)data;
	PipeWireBackend::Private *d = b->d;
	if (id != PW_ID_CORE || seq != d->syncSeq)
		return;
	/* round 1: the registry is enumerated; round 2: the bound objects
	 * have sent their info and params */
	if (++d->syncRound < 2) {
		d->syncSeq = pw_core_sync(d->core, PW_ID_CORE, 0);
		return;
	}
	if (!b->rawModel().connected) {
		b->rawModel().connected = true;
		rebuild(b);
	}
}

static void coreError(void *data, uint32_t id, int, int res, const char *message)
{
	PipeWireBackend *b = (PipeWireBackend *)data;
	if (id == PW_ID_CORE && res == -EPIPE) {
		/* the daemon went away */
		fprintf(stderr, "tde-mixer: PipeWire connection lost: %s\n", message ? message : "");
		b->d->failed = true;
		b->rawModel().clear();
		b->requestSync();
	}
}

/* level meters ------------------------------------------------------ */

static void peakProcess(void *data)
{
	PwPeak *pk = (PwPeak *)data;
	struct pw_buffer *buf = pw_stream_dequeue_buffer(pk->stream);
	if (!buf)
		return;
	struct spa_buffer *sb = buf->buffer;
	float v = 0.0f;
	if (sb->n_datas > 0 && sb->datas[0].data && sb->datas[0].chunk) {
		uint32_t off = SPA_MIN(sb->datas[0].chunk->offset, sb->datas[0].maxsize);
		uint32_t size = SPA_MIN(sb->datas[0].chunk->size, sb->datas[0].maxsize - off);
		const float *f = (const float *)((const uint8_t *)sb->datas[0].data + off);
		for (uint32_t i = 0; i < size / sizeof(float); ++i) {
			float a = fabsf(f[i]);
			if (a > v)
				v = a;
		}
	}
	pw_stream_queue_buffer(pk->stream, buf);
	if (v > 1.0f)
		v = 1.0f;
	float &slot = pk->backend->rawPeaks()[pk->id];
	slot = std::max(slot, v);
}

static void destroyPeak(PwPeak *pk)
{
	spa_hook_remove(&pk->listener);
	pw_stream_destroy(pk->stream);
	delete pk;
}

static void initEvents()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	memset(&s_nodeEvents, 0, sizeof(s_nodeEvents));
	s_nodeEvents.version = PW_VERSION_NODE_EVENTS;
	s_nodeEvents.info = nodeInfo;
	s_nodeEvents.param = nodeParam;
	memset(&s_deviceEvents, 0, sizeof(s_deviceEvents));
	s_deviceEvents.version = PW_VERSION_DEVICE_EVENTS;
	s_deviceEvents.info = deviceInfo;
	s_deviceEvents.param = deviceParam;
	memset(&s_metadataEvents, 0, sizeof(s_metadataEvents));
	s_metadataEvents.version = PW_VERSION_METADATA_EVENTS;
	s_metadataEvents.property = metadataProperty;
	memset(&s_registryEvents, 0, sizeof(s_registryEvents));
	s_registryEvents.version = PW_VERSION_REGISTRY_EVENTS;
	s_registryEvents.global = registryGlobal;
	s_registryEvents.global_remove = registryGlobalRemove;
	memset(&s_coreEvents, 0, sizeof(s_coreEvents));
	s_coreEvents.version = PW_VERSION_CORE_EVENTS;
	s_coreEvents.info = coreInfo;
	s_coreEvents.done = coreDone;
	s_coreEvents.error = coreError;
	memset(&s_peakEvents, 0, sizeof(s_peakEvents));
	s_peakEvents.version = PW_VERSION_STREAM_EVENTS;
	s_peakEvents.process = peakProcess;
}

/* ------------------------------------------------------------------ */

PipeWireBackend::PipeWireBackend(TQObject *parent)
	: AudioBackend(parent, "PipeWireBackend"), d(new Private)
{
	pw_init(0, 0);
	initEvents();
}

PipeWireBackend::~PipeWireBackend()
{
	if (d->loop) {
		pw_thread_loop_lock(d->loop);
		disconnectCore();
		pw_thread_loop_unlock(d->loop);
		pw_thread_loop_stop(d->loop);
		if (d->context)
			pw_context_destroy(d->context);
		pw_thread_loop_destroy(d->loop);
	}
	delete d;
}

void PipeWireBackend::lock()
{
	pw_thread_loop_lock(d->loop);
}

void PipeWireBackend::unlock()
{
	pw_thread_loop_unlock(d->loop);
}

bool PipeWireBackend::start()
{
	d->loop = pw_thread_loop_new("tde-mixer", 0);
	if (!d->loop)
		return false;
	d->context = pw_context_new(pw_thread_loop_get_loop(d->loop),
	                            pw_properties_new(PW_KEY_APP_NAME, "TDE Mixer",
	                                              PW_KEY_APP_ID, "org.trinitydesktop.tde-mixer",
	                                              PW_KEY_APP_ICON_NAME, "tde-mixer",
	                                              (const char *)0), 0);
	if (!d->context)
		return false;
	if (pw_thread_loop_start(d->loop) < 0)
		return false;

	lock();
	bool ok = connectCore();
	unlock();
	return ok;
}

bool PipeWireBackend::connectCore()
{
	/* lock held */
	d->core = pw_context_connect(d->context, 0, 0);
	if (!d->core) {
		/* afterSync() retries later, if this backend is kept */
		d->failed = true;
		scheduleSync();
		return false;
	}
	d->failed = false;
	d->syncRound = 0;
	pw_core_add_listener(d->core, &d->coreListener, &s_coreEvents, this);
	d->registry = pw_core_get_registry(d->core, PW_VERSION_REGISTRY, 0);
	pw_registry_add_listener(d->registry, &d->registryListener, &s_registryEvents, this);
	d->syncSeq = pw_core_sync(d->core, PW_ID_CORE, 0);
	return true;
}

void PipeWireBackend::disconnectCore()
{
	/* lock held */
	for (std::map<uint32_t, PwPeak *>::iterator it = d->peaks.begin(); it != d->peaks.end(); ++it)
		destroyPeak(it->second);
	d->peaks.clear();
	m_rawPeaks.clear();
	for (std::map<uint32_t, PwObject *>::iterator it = d->objects.begin(); it != d->objects.end(); ++it)
		destroyObject(it->second);
	d->objects.clear();
	d->metadata = 0;
	d->defaultSink.clear();
	d->defaultSource.clear();
	d->configuredSink.clear();
	d->configuredSource.clear();
	if (d->registry) {
		spa_hook_remove(&d->registryListener);
		pw_proxy_destroy((pw_proxy *)d->registry);
		d->registry = 0;
	}
	if (d->core) {
		spa_hook_remove(&d->coreListener);
		pw_core_disconnect(d->core);
		d->core = 0;
	}
	m_raw.clear();
}

void PipeWireBackend::reconnect()
{
	lock();
	d->reconnectScheduled = false;
	disconnectCore();
	if (!connectCore())
		scheduleSync();   /* try again later */
	unlock();
}

void PipeWireBackend::afterSync()
{
	/* GUI thread, lock held */
	if (d->failed) {
		if (!d->reconnectScheduled) {
			d->reconnectScheduled = true;
			TQTimer::singleShot(2000, this, TQ_SLOT(reconnect()));
		}
		return;
	}
	updatePeakStreams();
}

void PipeWireBackend::setPeaksEnabled(bool enabled)
{
	AudioBackend::setPeaksEnabled(enabled);
	lock();
	updatePeakStreams();
	unlock();
}

void PipeWireBackend::updatePeakStreams()
{
	/* lock held */
	std::map<uint32_t, bool> wanted;  // id -> is a sink
	if (m_peaksEnabled && d->core && m_raw.connected) {
		for (std::map<uint32_t, Audio::Device>::const_iterator it = m_raw.devices.begin(); it != m_raw.devices.end(); ++it)
			wanted[it->first] = it->second.kind == Audio::Sink;
		for (std::map<uint32_t, Audio::Stream>::const_iterator it = m_raw.streams.begin(); it != m_raw.streams.end(); ++it)
			if (it->second.kind == Audio::Playback)
				wanted[it->first] = false;
	}

	for (std::map<uint32_t, PwPeak *>::iterator it = d->peaks.begin(); it != d->peaks.end();) {
		if (wanted.count(it->first)) {
			++it;
		} else {
			m_rawPeaks.erase(it->first);
			destroyPeak(it->second);
			d->peaks.erase(it++);
		}
	}

	for (std::map<uint32_t, bool>::iterator w = wanted.begin(); w != wanted.end(); ++w) {
		if (d->peaks.count(w->first))
			continue;
		std::map<uint32_t, PwObject *>::iterator o = d->objects.find(w->first);
		if (o == d->objects.end())
			continue;
		std::string serial = o->second->get(PW_KEY_OBJECT_SERIAL);
		if (serial.empty()) {
			char buf[16];
			snprintf(buf, sizeof(buf), "%u", w->first);
			serial = buf;
		}
		pw_properties *props = pw_properties_new(
			PW_KEY_MEDIA_TYPE, "Audio",
			PW_KEY_MEDIA_CATEGORY, "Monitor",
			PW_KEY_MEDIA_ROLE, "DSP",
			PW_KEY_APP_NAME, "TDE Mixer",
			PW_KEY_NODE_NAME, "tde-mixer-peak",
			PW_KEY_TARGET_OBJECT, serial.c_str(),
			PW_KEY_STREAM_MONITOR, "true",
			PW_KEY_NODE_DONT_RECONNECT, "true",
			PW_KEY_NODE_LATENCY, "1024/48000",
			PEAK_PROPERTY, "1",
			(const char *)0);
		if (w->second)
			pw_properties_set(props, PW_KEY_STREAM_CAPTURE_SINK, "true");
		/* passive: does not keep a device running; but a passive stream
		 * is not linked to an application's stream */
		if (m_raw.devices.count(w->first))
			pw_properties_set(props, PW_KEY_NODE_PASSIVE, "true");
		pw_stream *s = pw_stream_new(d->core, "tde-mixer peak", props);
		if (!s)
			continue;
		PwPeak *pk = new PwPeak;
		pk->backend = this;
		pk->id = w->first;
		pk->stream = s;
		pw_stream_add_listener(s, &pk->listener, &s_peakEvents, pk);

		uint8_t buffer[512];
		const struct spa_pod *params[1];
		params[0] = tm_build_peak_format(buffer, sizeof(buffer));
		if (pw_stream_connect(s, PW_DIRECTION_INPUT, PW_ID_ANY,
		                      (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
		                      params, 1) < 0) {
			destroyPeak(pk);
			continue;
		}
		d->peaks[w->first] = pk;
	}
}

/* ------------------------------------------------------------------ */
/* operations (GUI thread)                                            */

void PipeWireBackend::setNodeVolume(uint32_t id, const std::vector<float> *volumes, int mute)
{
	/* lock held */
	std::map<uint32_t, PwObject *>::iterator it = d->objects.find(id);
	if (it == d->objects.end() || it->second->kind != NodeObj || !it->second->proxy)
		return;
	PwObject *o = it->second;

	std::vector<float> linear;
	if (volumes)
		for (size_t i = 0; i < volumes->size(); ++i)
			linear.push_back(toLinear((*volumes)[i]));

	/* hardware device with an active route: set the route (saved by the
	 * session manager, and what pipewire-pulse does too) */
	std::string devId = o->get(PW_KEY_DEVICE_ID);
	std::string cpd = o->get("card.profile.device");
	if (!devId.empty() && !cpd.empty()) {
		std::map<uint32_t, PwObject *>::iterator c = d->objects.find((uint32_t)atoi(devId.c_str()));
		if (c != d->objects.end() && c->second->kind == DeviceObj && c->second->proxy) {
			int32_t devIndex = atoi(cpd.c_str());
			std::string mc = o->get(PW_KEY_MEDIA_CLASS);
			uint32_t dir = mc == "Audio/Sink" ? TM_DIRECTION_OUTPUT : TM_DIRECTION_INPUT;
			const std::vector<RouteInfo> &routes = c->second->activeRoutes;
			for (size_t i = 0; i < routes.size(); ++i) {
				if (routes[i].device != devIndex || routes[i].direction != dir)
					continue;
				if (linear.empty() && volumes)
					break;
				uint8_t buf[2048];
				const struct spa_pod *pod = tm_build_route(buf, sizeof(buf), routes[i].index, devIndex,
				                                           linear.empty() ? 0 : &linear[0], linear.size(),
				                                           mute, 1);
				if (pod)
					pw_device_set_param((pw_device *)c->second->proxy, tm_param_route(), 0, pod);
				return;
			}
		}
	}

	uint8_t buf[2048];
	const struct spa_pod *pod = tm_build_props(buf, sizeof(buf), linear.empty() ? 0 : &linear[0],
	                                           linear.size(), mute);
	if (pod)
		pw_node_set_param((pw_node *)o->proxy, tm_param_props(), 0, pod);
}

void PipeWireBackend::setDeviceVolume(uint32_t id, const std::vector<float> &volumes)
{
	lock();
	setNodeVolume(id, &volumes, -1);
	unlock();
}

void PipeWireBackend::setDeviceMute(uint32_t id, bool mute)
{
	lock();
	setNodeVolume(id, 0, mute ? 1 : 0);
	unlock();
}

void PipeWireBackend::setStreamVolume(uint32_t id, const std::vector<float> &volumes)
{
	lock();
	setNodeVolume(id, &volumes, -1);
	unlock();
}

void PipeWireBackend::setStreamMute(uint32_t id, bool mute)
{
	lock();
	setNodeVolume(id, 0, mute ? 1 : 0);
	unlock();
}

void PipeWireBackend::setDefaultDevice(uint32_t id)
{
	const Audio::Device *dev = device(id);
	if (!dev)
		return;
	std::string value = "{ \"name\": \"" + jsonEscape(dev->name) + "\" }";
	const char *key = dev->kind == Audio::Sink ? "default.configured.audio.sink"
	                                           : "default.configured.audio.source";
	lock();
	if (d->metadata && d->metadata->proxy)
		pw_metadata_set_property((pw_metadata *)d->metadata->proxy, PW_ID_CORE, key,
		                         "Spa:String:JSON", value.c_str());
	unlock();
}

void PipeWireBackend::moveStream(uint32_t streamId, uint32_t deviceId)
{
	lock();
	std::map<uint32_t, PwObject *>::iterator dev = d->objects.find(deviceId);
	if (d->metadata && d->metadata->proxy && dev != d->objects.end()) {
		char idbuf[16];
		snprintf(idbuf, sizeof(idbuf), "%u", deviceId);
		std::string serial = dev->second->get(PW_KEY_OBJECT_SERIAL);
		pw_metadata *md = (pw_metadata *)d->metadata->proxy;
		/* WirePlumber 0.5 uses target.object (a serial), 0.4 target.node */
		if (!serial.empty())
			pw_metadata_set_property(md, streamId, "target.object", "Spa:Id", serial.c_str());
		pw_metadata_set_property(md, streamId, "target.node", "Spa:Id", idbuf);
	}
	unlock();
}

void PipeWireBackend::setDevicePort(uint32_t id, const std::string &port)
{
	lock();
	std::map<uint32_t, PwObject *>::iterator it = d->objects.find(id);
	if (it != d->objects.end()) {
		PwObject *o = it->second;
		std::string devId = o->get(PW_KEY_DEVICE_ID);
		std::string cpd = o->get("card.profile.device");
		std::map<uint32_t, PwObject *>::iterator c = d->objects.find((uint32_t)atoi(devId.c_str()));
		if (!devId.empty() && !cpd.empty() && c != d->objects.end() && c->second->proxy) {
			const std::vector<RouteInfo> &routes = c->second->routes;
			for (size_t i = 0; i < routes.size(); ++i) {
				if (routes[i].name != port)
					continue;
				uint8_t buf[1024];
				const struct spa_pod *pod = tm_build_route(buf, sizeof(buf), routes[i].index,
				                                           atoi(cpd.c_str()), 0, 0, -1, 0);
				if (pod)
					pw_device_set_param((pw_device *)c->second->proxy, tm_param_route(), 0, pod);
				break;
			}
		}
	}
	unlock();
}

void PipeWireBackend::setCardProfile(uint32_t id, const std::string &profile)
{
	lock();
	std::map<uint32_t, PwObject *>::iterator it = d->objects.find(id);
	if (it != d->objects.end() && it->second->kind == DeviceObj && it->second->proxy) {
		const std::vector<ProfileInfo> &profiles = it->second->profiles;
		for (size_t i = 0; i < profiles.size(); ++i) {
			if (profiles[i].profile.name != profile)
				continue;
			uint8_t buf[512];
			const struct spa_pod *pod = tm_build_profile(buf, sizeof(buf), profiles[i].index);
			if (pod)
				pw_device_set_param((pw_device *)it->second->proxy, tm_param_profile(), 0, pod);
			break;
		}
	}
	unlock();
}

#include "pwbackend.moc"
