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

#include <pulse/pulseaudio.h>

#include "pulsebackend.h"

#include <algorithm>

#include <tqtimer.h>

#define PEAK_PROPERTY "tde-mixer.peak"

struct PeakStream {
	PulseBackend *backend;
	uint32_t id;        // model id of the device or stream
	uint32_t target;    // sink a sink input plays to (to notice moves)
	pa_stream *stream;
};

struct PulseBackend::Private {
	pa_threaded_mainloop *loop;
	pa_context *context;
	bool failed;              // context failed, reconnect from the GUI thread
	bool reconnectScheduled;
	uint32_t ownClient;
	std::map<uint32_t, PeakStream *> peaks;
	Private() : loop(0), context(0), failed(false), reconnectScheduled(false), ownClient(PA_INVALID_INDEX) {}
};

/* ------------------------------------------------------------------ */
/* helpers                                                            */

static std::string prop(pa_proplist *p, const char *key)
{
	const char *v = p ? pa_proplist_gets(p, key) : 0;
	return v ? std::string(v) : std::string();
}

static const char *shortChannelName(pa_channel_position_t p)
{
	/* the same short names PipeWire uses */
	switch (p) {
	case PA_CHANNEL_POSITION_MONO:         return "MONO";
	case PA_CHANNEL_POSITION_FRONT_LEFT:   return "FL";
	case PA_CHANNEL_POSITION_FRONT_RIGHT:  return "FR";
	case PA_CHANNEL_POSITION_FRONT_CENTER: return "FC";
	case PA_CHANNEL_POSITION_REAR_LEFT:    return "RL";
	case PA_CHANNEL_POSITION_REAR_RIGHT:   return "RR";
	case PA_CHANNEL_POSITION_REAR_CENTER:  return "RC";
	case PA_CHANNEL_POSITION_LFE:          return "LFE";
	case PA_CHANNEL_POSITION_SIDE_LEFT:    return "SL";
	case PA_CHANNEL_POSITION_SIDE_RIGHT:   return "SR";
	case PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER:  return "FLC";
	case PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER: return "FRC";
	default: return pa_channel_position_to_string(p);
	}
}

static void fillVolume(const pa_cvolume &cv, const pa_channel_map &map,
                       std::vector<float> &volumes, std::vector<std::string> &channels)
{
	volumes.clear();
	channels.clear();
	for (unsigned i = 0; i < cv.channels; ++i) {
		volumes.push_back((float)cv.values[i] / (float)PA_VOLUME_NORM);
		channels.push_back(i < map.channels ? shortChannelName(map.map[i]) : "");
	}
}

static pa_cvolume toCVolume(const std::vector<float> &volumes)
{
	pa_cvolume cv;
	pa_cvolume_init(&cv);
	cv.channels = volumes.size() > PA_CHANNELS_MAX ? PA_CHANNELS_MAX : volumes.size();
	for (unsigned i = 0; i < cv.channels; ++i) {
		float v = volumes[i] < 0 ? 0 : volumes[i];
		cv.values[i] = (pa_volume_t)(v * PA_VOLUME_NORM + 0.5f);
		if (cv.values[i] > PA_VOLUME_MAX)
			cv.values[i] = PA_VOLUME_MAX;
	}
	return cv;
}

static bool isHelperStream(pa_proplist *p)
{
	/* our own level meters and those of pavucontrol */
	if (p && pa_proplist_contains(p, PEAK_PROPERTY))
		return true;
	return prop(p, PA_PROP_APPLICATION_ID) == "org.PulseAudio.pavucontrol";
}

static void unref(pa_operation *o)
{
	if (o)
		pa_operation_unref(o);
}

/* ------------------------------------------------------------------ */
/* info callbacks (mainloop thread, lock held)                        */

static void sinkCb(pa_context *, const pa_sink_info *i, int eol, void *ud)
{
	PulseBackend *b = (PulseBackend *)ud;
	if (eol || !i)
		return;
	Audio::Device dev;
	dev.id = PulseBackend::makeId(PulseBackend::SinkId, i->index);
	dev.kind = Audio::Sink;
	dev.name = i->name ? i->name : "";
	dev.description = i->description ? i->description : dev.name;
	dev.iconName = prop(i->proplist, PA_PROP_DEVICE_ICON_NAME);
	dev.monitorName = i->monitor_source_name ? i->monitor_source_name : "";
	fillVolume(i->volume, i->channel_map, dev.volumes, dev.channels);
	dev.muted = i->mute;
	dev.hasVolume = true;
	dev.isVirtual = !(i->flags & PA_SINK_HARDWARE);
	for (uint32_t p = 0; p < i->n_ports; ++p) {
		Audio::Port port;
		port.name = i->ports[p]->name;
		port.description = i->ports[p]->description ? i->ports[p]->description : port.name;
		port.available = i->ports[p]->available != PA_PORT_AVAILABLE_NO;
		dev.ports.push_back(port);
	}
	if (i->active_port)
		dev.activePort = i->active_port->name;
	dev.cardId = i->card == PA_INVALID_INDEX ? Audio::InvalidId
	                                         : PulseBackend::makeId(PulseBackend::CardId, i->card);
	b->rawModel().devices[dev.id] = dev;
	b->requestSync();
}

static void sourceCb(pa_context *, const pa_source_info *i, int eol, void *ud)
{
	PulseBackend *b = (PulseBackend *)ud;
	if (eol || !i)
		return;
	Audio::Device dev;
	dev.id = PulseBackend::makeId(PulseBackend::SourceId, i->index);
	dev.kind = Audio::Source;
	dev.name = i->name ? i->name : "";
	dev.description = i->description ? i->description : dev.name;
	dev.iconName = prop(i->proplist, PA_PROP_DEVICE_ICON_NAME);
	fillVolume(i->volume, i->channel_map, dev.volumes, dev.channels);
	dev.muted = i->mute;
	dev.hasVolume = true;
	dev.isMonitor = i->monitor_of_sink != PA_INVALID_INDEX;
	dev.isVirtual = !(i->flags & PA_SOURCE_HARDWARE);
	for (uint32_t p = 0; p < i->n_ports; ++p) {
		Audio::Port port;
		port.name = i->ports[p]->name;
		port.description = i->ports[p]->description ? i->ports[p]->description : port.name;
		port.available = i->ports[p]->available != PA_PORT_AVAILABLE_NO;
		dev.ports.push_back(port);
	}
	if (i->active_port)
		dev.activePort = i->active_port->name;
	dev.cardId = i->card == PA_INVALID_INDEX ? Audio::InvalidId
	                                         : PulseBackend::makeId(PulseBackend::CardId, i->card);
	b->rawModel().devices[dev.id] = dev;
	b->requestSync();
}

static void fillStream(Audio::Stream &s, pa_proplist *p, const char *name)
{
	s.appName = prop(p, PA_PROP_APPLICATION_NAME);
	if (s.appName.empty())
		s.appName = name ? name : "";
	s.binary = prop(p, PA_PROP_APPLICATION_PROCESS_BINARY);
	s.iconName = prop(p, PA_PROP_APPLICATION_ICON_NAME);
	s.mediaName = prop(p, PA_PROP_MEDIA_NAME);
	if (s.mediaName.empty() && name)
		s.mediaName = name;
	s.role = prop(p, PA_PROP_MEDIA_ROLE);
}

static void sinkInputCb(pa_context *, const pa_sink_input_info *i, int eol, void *ud)
{
	PulseBackend *b = (PulseBackend *)ud;
	if (eol || !i)
		return;
	uint32_t id = PulseBackend::makeId(PulseBackend::SinkInputId, i->index);
	if (isHelperStream(i->proplist) || (i->client != PA_INVALID_INDEX && i->client == b->d->ownClient)) {
		b->rawModel().streams.erase(id);
		return;
	}
	Audio::Stream s;
	s.id = id;
	s.kind = Audio::Playback;
	fillStream(s, i->proplist, i->name);
	s.deviceId = PulseBackend::makeId(PulseBackend::SinkId, i->sink);
	fillVolume(i->volume, i->channel_map, s.volumes, s.channels);
	s.muted = i->mute;
	s.hasVolume = i->has_volume && i->volume_writable;
	s.corked = i->corked;
	b->rawModel().streams[id] = s;
	b->requestSync();
}

static void sourceOutputCb(pa_context *, const pa_source_output_info *i, int eol, void *ud)
{
	PulseBackend *b = (PulseBackend *)ud;
	if (eol || !i)
		return;
	uint32_t id = PulseBackend::makeId(PulseBackend::SourceOutputId, i->index);
	if (isHelperStream(i->proplist) || (i->client != PA_INVALID_INDEX && i->client == b->d->ownClient)) {
		b->rawModel().streams.erase(id);
		return;
	}
	Audio::Stream s;
	s.id = id;
	s.kind = Audio::Recording;
	fillStream(s, i->proplist, i->name);
	s.deviceId = PulseBackend::makeId(PulseBackend::SourceId, i->source);
	fillVolume(i->volume, i->channel_map, s.volumes, s.channels);
	s.muted = i->mute;
	s.hasVolume = i->has_volume && i->volume_writable;
	s.corked = i->corked;
	b->rawModel().streams[id] = s;
	b->requestSync();
}

static void cardCb(pa_context *, const pa_card_info *i, int eol, void *ud)
{
	PulseBackend *b = (PulseBackend *)ud;
	if (eol || !i)
		return;
	Audio::Card c;
	c.id = PulseBackend::makeId(PulseBackend::CardId, i->index);
	c.name = i->name ? i->name : "";
	c.description = prop(i->proplist, PA_PROP_DEVICE_DESCRIPTION);
	if (c.description.empty())
		c.description = c.name;
	c.iconName = prop(i->proplist, PA_PROP_DEVICE_ICON_NAME);
	for (uint32_t p = 0; p < i->n_profiles; ++p) {
		Audio::Profile pr;
		pr.name = i->profiles2[p]->name;
		pr.description = i->profiles2[p]->description ? i->profiles2[p]->description : pr.name;
		pr.available = i->profiles2[p]->available != 0;
		pr.priority = i->profiles2[p]->priority;
		c.profiles.push_back(pr);
	}
	if (i->active_profile2)
		c.activeProfile = i->active_profile2->name;
	b->rawModel().cards[c.id] = c;
	b->requestSync();
}

static void serverCb(pa_context *, const pa_server_info *i, void *ud)
{
	PulseBackend *b = (PulseBackend *)ud;
	if (!i)
		return;
	Audio::Model &m = b->rawModel();
	m.defaultSink = i->default_sink_name ? i->default_sink_name : "";
	m.defaultSource = i->default_source_name ? i->default_source_name : "";
	m.serverName = std::string(i->server_name ? i->server_name : "PulseAudio") + " " +
	               (i->server_version ? i->server_version : "");
	b->requestSync();
}

static void subscribeCb(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *ud)
{
	PulseBackend *b = (PulseBackend *)ud;
	unsigned facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
	bool removed = (t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE;
	Audio::Model &m = b->rawModel();

	switch (facility) {
	case PA_SUBSCRIPTION_EVENT_SINK:
		if (removed) m.devices.erase(PulseBackend::makeId(PulseBackend::SinkId, idx));
		else unref(pa_context_get_sink_info_by_index(c, idx, sinkCb, b));
		break;
	case PA_SUBSCRIPTION_EVENT_SOURCE:
		if (removed) m.devices.erase(PulseBackend::makeId(PulseBackend::SourceId, idx));
		else unref(pa_context_get_source_info_by_index(c, idx, sourceCb, b));
		break;
	case PA_SUBSCRIPTION_EVENT_SINK_INPUT:
		if (removed) m.streams.erase(PulseBackend::makeId(PulseBackend::SinkInputId, idx));
		else unref(pa_context_get_sink_input_info(c, idx, sinkInputCb, b));
		break;
	case PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT:
		if (removed) m.streams.erase(PulseBackend::makeId(PulseBackend::SourceOutputId, idx));
		else unref(pa_context_get_source_output_info(c, idx, sourceOutputCb, b));
		break;
	case PA_SUBSCRIPTION_EVENT_CARD:
		if (removed) m.cards.erase(PulseBackend::makeId(PulseBackend::CardId, idx));
		else unref(pa_context_get_card_info_by_index(c, idx, cardCb, b));
		break;
	case PA_SUBSCRIPTION_EVENT_SERVER:
		unref(pa_context_get_server_info(c, serverCb, b));
		break;
	}
	if (removed)
		b->requestSync();
}

static void stateCb(pa_context *c, void *ud)
{
	PulseBackend *b = (PulseBackend *)ud;
	switch (pa_context_get_state(c)) {
	case PA_CONTEXT_READY:
		b->d->ownClient = pa_context_get_index(c);
		b->rawModel().connected = true;
		pa_context_set_subscribe_callback(c, subscribeCb, b);
		unref(pa_context_subscribe(c, (pa_subscription_mask_t)
		      (PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE |
		       PA_SUBSCRIPTION_MASK_SINK_INPUT | PA_SUBSCRIPTION_MASK_SOURCE_OUTPUT |
		       PA_SUBSCRIPTION_MASK_CARD | PA_SUBSCRIPTION_MASK_SERVER), 0, 0));
		unref(pa_context_get_server_info(c, serverCb, b));
		unref(pa_context_get_card_info_list(c, cardCb, b));
		unref(pa_context_get_sink_info_list(c, sinkCb, b));
		unref(pa_context_get_source_info_list(c, sourceCb, b));
		unref(pa_context_get_sink_input_info_list(c, sinkInputCb, b));
		unref(pa_context_get_source_output_info_list(c, sourceOutputCb, b));
		b->requestSync();
		break;
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_TERMINATED:
		b->rawModel().clear();
		b->d->failed = true;
		b->requestSync();
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* level meters                                                       */

static void peakReadCb(pa_stream *s, size_t, void *ud)
{
	PeakStream *ps = (PeakStream *)ud;
	const void *data;
	size_t n;
	if (pa_stream_peek(s, &data, &n) < 0)
		return;
	if (!data) {
		if (n)
			pa_stream_drop(s);
		return;
	}
	float v = 0.0f;
	const float *f = (const float *)data;
	for (size_t i = 0; i < n / sizeof(float); ++i)
		if (f[i] > v)
			v = f[i];
	pa_stream_drop(s);
	if (v > 1.0f)
		v = 1.0f;
	ps->backend->rawPeaks()[ps->id] = std::max(ps->backend->rawPeaks()[ps->id], v);
}

static PeakStream *createPeakStream(PulseBackend *b, pa_context *c, uint32_t id,
                                    const char *source, uint32_t sinkInput, uint32_t target)
{
	pa_sample_spec ss;
	ss.channels = 1;
	ss.format = PA_SAMPLE_FLOAT32;
	ss.rate = 25;

	pa_proplist *p = pa_proplist_new();
	pa_proplist_sets(p, PEAK_PROPERTY, "1");
	pa_proplist_sets(p, PA_PROP_APPLICATION_ID, "org.trinitydesktop.tde-mixer");
	pa_stream *s = pa_stream_new_with_proplist(c, "Peak detect", &ss, 0, p);
	pa_proplist_free(p);
	if (!s)
		return 0;

	PeakStream *ps = new PeakStream;
	ps->backend = b;
	ps->id = id;
	ps->target = target;
	ps->stream = s;
	if (sinkInput != PA_INVALID_INDEX)
		pa_stream_set_monitor_stream(s, sinkInput);
	pa_stream_set_read_callback(s, peakReadCb, ps);

	pa_buffer_attr attr;
	attr.maxlength = (uint32_t)-1;
	attr.fragsize = sizeof(float);
	attr.tlength = attr.prebuf = attr.minreq = (uint32_t)-1;
	if (pa_stream_connect_record(s, source, &attr, (pa_stream_flags_t)
	        (PA_STREAM_DONT_MOVE | PA_STREAM_PEAK_DETECT | PA_STREAM_ADJUST_LATENCY |
	         PA_STREAM_DONT_INHIBIT_AUTO_SUSPEND)) < 0) {
		pa_stream_unref(s);
		delete ps;
		return 0;
	}
	return ps;
}

static void destroyPeakStream(PeakStream *ps)
{
	pa_stream_set_read_callback(ps->stream, 0, 0);
	pa_stream_disconnect(ps->stream);
	pa_stream_unref(ps->stream);
	delete ps;
}

/* ------------------------------------------------------------------ */

PulseBackend::PulseBackend(TQObject *parent)
	: AudioBackend(parent, "PulseBackend"), d(new Private)
{
}

PulseBackend::~PulseBackend()
{
	if (d->loop) {
		pa_threaded_mainloop_lock(d->loop);
		for (std::map<uint32_t, PeakStream *>::iterator it = d->peaks.begin(); it != d->peaks.end(); ++it)
			destroyPeakStream(it->second);
		d->peaks.clear();
		if (d->context) {
			pa_context_set_state_callback(d->context, 0, 0);
			pa_context_set_subscribe_callback(d->context, 0, 0);
			pa_context_disconnect(d->context);
			pa_context_unref(d->context);
			d->context = 0;
		}
		pa_threaded_mainloop_unlock(d->loop);
		pa_threaded_mainloop_stop(d->loop);
		pa_threaded_mainloop_free(d->loop);
	}
	delete d;
}

void PulseBackend::lock()
{
	pa_threaded_mainloop_lock(d->loop);
}

void PulseBackend::unlock()
{
	pa_threaded_mainloop_unlock(d->loop);
}

bool PulseBackend::start()
{
	d->loop = pa_threaded_mainloop_new();
	if (!d->loop)
		return false;
	pa_threaded_mainloop_lock(d->loop);
	connectContext();
	pa_threaded_mainloop_unlock(d->loop);
	return pa_threaded_mainloop_start(d->loop) == 0;
}

void PulseBackend::connectContext()
{
	/* lock held */
	pa_proplist *p = pa_proplist_new();
	pa_proplist_sets(p, PA_PROP_APPLICATION_NAME, "TDE Mixer");
	pa_proplist_sets(p, PA_PROP_APPLICATION_ID, "org.trinitydesktop.tde-mixer");
	pa_proplist_sets(p, PA_PROP_APPLICATION_ICON_NAME, "tde-mixer");
	pa_proplist_sets(p, PA_PROP_APPLICATION_VERSION, TDE_MIXER_VERSION);
	d->context = pa_context_new_with_proplist(pa_threaded_mainloop_get_api(d->loop), "TDE Mixer", p);
	pa_proplist_free(p);
	d->failed = false;
	pa_context_set_state_callback(d->context, stateCb, this);
	/* NOFAIL: wait for a server that is not running yet instead of failing */
	if (pa_context_connect(d->context, 0, PA_CONTEXT_NOFAIL, 0) < 0) {
		d->failed = true;
		scheduleSync();
	}
}

void PulseBackend::reconnect()
{
	lock();
	d->reconnectScheduled = false;
	for (std::map<uint32_t, PeakStream *>::iterator it = d->peaks.begin(); it != d->peaks.end(); ++it)
		destroyPeakStream(it->second);
	d->peaks.clear();
	m_rawPeaks.clear();
	if (d->context) {
		pa_context_set_state_callback(d->context, 0, 0);
		pa_context_set_subscribe_callback(d->context, 0, 0);
		pa_context_disconnect(d->context);
		pa_context_unref(d->context);
		d->context = 0;
	}
	connectContext();
	unlock();
}

void PulseBackend::afterSync()
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

void PulseBackend::setPeaksEnabled(bool enabled)
{
	AudioBackend::setPeaksEnabled(enabled);
	lock();
	updatePeakStreams();
	unlock();
}

void PulseBackend::updatePeakStreams()
{
	/* lock held */
	pa_context *c = d->context;
	bool ready = c && pa_context_get_state(c) == PA_CONTEXT_READY;

	/* which meters are wanted: devices and playback streams */
	std::map<uint32_t, uint32_t> wanted; // id -> target sink (for sink inputs)
	if (ready && m_peaksEnabled) {
		for (std::map<uint32_t, Audio::Device>::const_iterator it = m_raw.devices.begin(); it != m_raw.devices.end(); ++it)
			if (!it->second.isMonitor)
				wanted[it->first] = Audio::InvalidId;
		for (std::map<uint32_t, Audio::Stream>::const_iterator it = m_raw.streams.begin(); it != m_raw.streams.end(); ++it)
			if (it->second.kind == Audio::Playback)
				wanted[it->first] = it->second.deviceId;
	}

	/* drop meters that are no longer wanted, or whose stream moved */
	for (std::map<uint32_t, PeakStream *>::iterator it = d->peaks.begin(); it != d->peaks.end();) {
		std::map<uint32_t, uint32_t>::iterator w = wanted.find(it->first);
		bool keep = w != wanted.end() && w->second == it->second->target &&
		            pa_stream_get_state(it->second->stream) != PA_STREAM_FAILED &&
		            pa_stream_get_state(it->second->stream) != PA_STREAM_TERMINATED;
		if (keep) {
			++it;
		} else {
			m_rawPeaks.erase(it->first);
			destroyPeakStream(it->second);
			d->peaks.erase(it++);
		}
	}

	for (std::map<uint32_t, uint32_t>::iterator w = wanted.begin(); w != wanted.end(); ++w) {
		if (d->peaks.count(w->first))
			continue;
		PeakStream *ps = 0;
		IdType t = typeOf(w->first);
		if (t == SinkId) {
			const Audio::Device &dev = m_raw.devices[w->first];
			if (!dev.monitorName.empty())
				ps = createPeakStream(this, c, w->first, dev.monitorName.c_str(), PA_INVALID_INDEX, w->second);
		} else if (t == SourceId) {
			ps = createPeakStream(this, c, w->first, m_raw.devices[w->first].name.c_str(), PA_INVALID_INDEX, w->second);
		} else if (t == SinkInputId) {
			std::map<uint32_t, Audio::Device>::const_iterator sink = m_raw.devices.find(w->second);
			if (sink != m_raw.devices.end() && !sink->second.monitorName.empty())
				ps = createPeakStream(this, c, w->first, sink->second.monitorName.c_str(), indexOf(w->first), w->second);
		}
		if (ps)
			d->peaks[w->first] = ps;
	}
}

/* ------------------------------------------------------------------ */
/* operations (GUI thread)                                            */

#define OP(expr) do { lock(); if (d->context && pa_context_get_state(d->context) == PA_CONTEXT_READY) unref(expr); unlock(); } while (0)

void PulseBackend::setDeviceVolume(uint32_t id, const std::vector<float> &volumes)
{
	pa_cvolume cv = toCVolume(volumes);
	if (typeOf(id) == SinkId)
		OP(pa_context_set_sink_volume_by_index(d->context, indexOf(id), &cv, 0, 0));
	else if (typeOf(id) == SourceId)
		OP(pa_context_set_source_volume_by_index(d->context, indexOf(id), &cv, 0, 0));
}

void PulseBackend::setDeviceMute(uint32_t id, bool mute)
{
	if (typeOf(id) == SinkId)
		OP(pa_context_set_sink_mute_by_index(d->context, indexOf(id), mute, 0, 0));
	else if (typeOf(id) == SourceId)
		OP(pa_context_set_source_mute_by_index(d->context, indexOf(id), mute, 0, 0));
}

void PulseBackend::setStreamVolume(uint32_t id, const std::vector<float> &volumes)
{
	pa_cvolume cv = toCVolume(volumes);
	if (typeOf(id) == SinkInputId)
		OP(pa_context_set_sink_input_volume(d->context, indexOf(id), &cv, 0, 0));
	else if (typeOf(id) == SourceOutputId)
		OP(pa_context_set_source_output_volume(d->context, indexOf(id), &cv, 0, 0));
}

void PulseBackend::setStreamMute(uint32_t id, bool mute)
{
	if (typeOf(id) == SinkInputId)
		OP(pa_context_set_sink_input_mute(d->context, indexOf(id), mute, 0, 0));
	else if (typeOf(id) == SourceOutputId)
		OP(pa_context_set_source_output_mute(d->context, indexOf(id), mute, 0, 0));
}

void PulseBackend::setDefaultDevice(uint32_t id)
{
	const Audio::Device *dev = device(id);
	if (!dev)
		return;
	std::string name = dev->name;
	if (typeOf(id) == SinkId)
		OP(pa_context_set_default_sink(d->context, name.c_str(), 0, 0));
	else if (typeOf(id) == SourceId)
		OP(pa_context_set_default_source(d->context, name.c_str(), 0, 0));
}

void PulseBackend::moveStream(uint32_t streamId, uint32_t deviceId)
{
	if (typeOf(streamId) == SinkInputId && typeOf(deviceId) == SinkId)
		OP(pa_context_move_sink_input_by_index(d->context, indexOf(streamId), indexOf(deviceId), 0, 0));
	else if (typeOf(streamId) == SourceOutputId && typeOf(deviceId) == SourceId)
		OP(pa_context_move_source_output_by_index(d->context, indexOf(streamId), indexOf(deviceId), 0, 0));
}

void PulseBackend::setDevicePort(uint32_t id, const std::string &port)
{
	if (typeOf(id) == SinkId)
		OP(pa_context_set_sink_port_by_index(d->context, indexOf(id), port.c_str(), 0, 0));
	else if (typeOf(id) == SourceId)
		OP(pa_context_set_source_port_by_index(d->context, indexOf(id), port.c_str(), 0, 0));
}

void PulseBackend::setCardProfile(uint32_t id, const std::string &profile)
{
	if (typeOf(id) == CardId)
		OP(pa_context_set_card_profile_by_index(d->context, indexOf(id), profile.c_str(), 0, 0));
}

#include "pulsebackend.moc"
