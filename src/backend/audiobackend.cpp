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

#include "audiobackend.h"

#ifdef HAVE_PIPEWIRE
#include "pwbackend.h"
#endif
#ifdef HAVE_PULSE
#include "pulsebackend.h"
#endif

#include <algorithm>

#include <tqapplication.h>

static const int SyncEvent = TQEvent::User + 0x4d58; /* "MX" */

void Audio::Model::clear()
{
	devices.clear();
	streams.clear();
	cards.clear();
	defaultSink.clear();
	defaultSource.clear();
	serverName.clear();
	connected = false;
}

float Audio::level(const std::vector<float> &volumes)
{
	float max = 0.0f;
	for (size_t i = 0; i < volumes.size(); ++i)
		max = std::max(max, volumes[i]);
	return max;
}

std::vector<float> Audio::scaled(const std::vector<float> &volumes, float level)
{
	std::vector<float> out(volumes);
	if (out.empty())
		out.push_back(level);
	if (level < 0.0f)
		level = 0.0f;
	float max = Audio::level(out);
	for (size_t i = 0; i < out.size(); ++i)
		out[i] = (max <= 0.0001f) ? level : out[i] * level / max;
	return out;
}

TQString Audio::q(const std::string &s)
{
	return TQString::fromUtf8(s.c_str(), s.size());
}

/* ------------------------------------------------------------------ */

AudioBackend::AudioBackend(TQObject *parent, const char *name)
	: TQObject(parent, name), m_peaksEnabled(false), m_syncPending(false)
{
}

AudioBackend::~AudioBackend()
{
}

AudioBackend::Type AudioBackend::typeFromString(const TQString &name)
{
	TQString n = name.lower();
	if (n == "pipewire")
		return PipeWire;
	if (n == "pulse" || n == "pulseaudio")
		return Pulse;
	return Auto;
}

TQString AudioBackend::typeToString(Type type)
{
	switch (type) {
	case PipeWire: return "pipewire";
	case Pulse:    return "pulse";
	default:       return "auto";
	}
}

TQStringList AudioBackend::compiledBackends()
{
	TQStringList l;
#ifdef HAVE_PIPEWIRE
	l << "pipewire";
#endif
#ifdef HAVE_PULSE
	l << "pulse";
#endif
	return l;
}

AudioBackend *AudioBackend::create(Type type, TQObject *parent)
{
#ifdef HAVE_PIPEWIRE
	if (type == PipeWire || type == Auto) {
		PipeWireBackend *pw = new PipeWireBackend(parent);
		if (pw->start() || type == PipeWire)
			return pw;
		delete pw;
	}
#endif
#ifdef HAVE_PULSE
	{
		PulseBackend *pa = new PulseBackend(parent);
		pa->start();
		return pa;
	}
#else
	(void)type;
	(void)parent;
	return 0;
#endif
}

void AudioBackend::scheduleSync()
{
	/* called with the lock held */
	if (m_syncPending)
		return;
	m_syncPending = true;
	TQApplication::postEvent(this, new TQCustomEvent(SyncEvent));
}

void AudioBackend::customEvent(TQCustomEvent *e)
{
	if (e->type() != SyncEvent)
		return;

	bool wasConnected = m_model.connected;
	lock();
	m_syncPending = false;
	m_model = m_raw;   /* std containers: a deep copy */
	afterSync();
	unlock();

	emit changed();
	if (wasConnected != m_model.connected)
		emit connectionChanged(m_model.connected);
}

std::vector<const Audio::Device *> AudioBackend::devices(Audio::DeviceKind kind, bool includeMonitors) const
{
	std::vector<const Audio::Device *> out;
	for (std::map<uint32_t, Audio::Device>::const_iterator it = m_model.devices.begin();
	     it != m_model.devices.end(); ++it) {
		if (it->second.kind != kind)
			continue;
		if (it->second.isMonitor && !includeMonitors)
			continue;
		out.push_back(&it->second);
	}
	return out;
}

std::vector<const Audio::Stream *> AudioBackend::streams(Audio::StreamKind kind) const
{
	std::vector<const Audio::Stream *> out;
	for (std::map<uint32_t, Audio::Stream>::const_iterator it = m_model.streams.begin();
	     it != m_model.streams.end(); ++it)
		if (it->second.kind == kind)
			out.push_back(&it->second);
	return out;
}

const Audio::Device *AudioBackend::device(uint32_t id) const
{
	std::map<uint32_t, Audio::Device>::const_iterator it = m_model.devices.find(id);
	return it == m_model.devices.end() ? 0 : &it->second;
}

const Audio::Stream *AudioBackend::stream(uint32_t id) const
{
	std::map<uint32_t, Audio::Stream>::const_iterator it = m_model.streams.find(id);
	return it == m_model.streams.end() ? 0 : &it->second;
}

const Audio::Card *AudioBackend::card(uint32_t id) const
{
	std::map<uint32_t, Audio::Card>::const_iterator it = m_model.cards.find(id);
	return it == m_model.cards.end() ? 0 : &it->second;
}

const Audio::Device *AudioBackend::defaultDevice(Audio::DeviceKind kind) const
{
	const std::string &name = kind == Audio::Sink ? m_model.defaultSink : m_model.defaultSource;
	const Audio::Device *first = 0;
	for (std::map<uint32_t, Audio::Device>::const_iterator it = m_model.devices.begin();
	     it != m_model.devices.end(); ++it) {
		const Audio::Device &d = it->second;
		if (d.kind != kind || d.isMonitor)
			continue;
		if (d.name == name)
			return &d;
		if (!first)
			first = &d;
	}
	return name.empty() ? first : 0;
}

void AudioBackend::setPeaksEnabled(bool enabled)
{
	m_peaksEnabled = enabled;
	if (!enabled) {
		lock();
		m_rawPeaks.clear();
		unlock();
	}
}

float AudioBackend::peak(uint32_t id)
{
	float v = -1.0f;
	lock();
	std::map<uint32_t, float>::iterator it = m_rawPeaks.find(id);
	if (it != m_rawPeaks.end()) {
		v = it->second;
		/* consumed: the meter falls back unless new data arrives */
		it->second = 0.0f;
	}
	unlock();
	return v;
}

void AudioBackend::setDeviceLevel(uint32_t id, float level)
{
	const Audio::Device *d = device(id);
	if (d)
		setDeviceVolume(id, Audio::scaled(d->volumes, level));
}

void AudioBackend::setStreamLevel(uint32_t id, float level)
{
	const Audio::Stream *s = stream(id);
	if (s)
		setStreamVolume(id, Audio::scaled(s->volumes, level));
}

void AudioBackend::changeDefaultVolume(Audio::DeviceKind kind, float delta, float max)
{
	const Audio::Device *d = defaultDevice(kind);
	if (!d)
		return;
	float l = Audio::level(d->volumes) + delta;
	l = std::max(0.0f, std::min(l, max));
	/* snap to the step so repeated presses land on round numbers */
	if (delta != 0.0f) {
		float step = delta < 0 ? -delta : delta;
		l = (float)((int)(l / step + 0.5f)) * step;
		l = std::max(0.0f, std::min(l, max));
	}
	setDeviceVolume(d->id, Audio::scaled(d->volumes, l));
	if (d->muted && delta > 0)
		setDeviceMute(d->id, false);
}

void AudioBackend::toggleDefaultMute(Audio::DeviceKind kind)
{
	const Audio::Device *d = defaultDevice(kind);
	if (d)
		setDeviceMute(d->id, !d->muted);
}

#include "audiobackend.moc"
