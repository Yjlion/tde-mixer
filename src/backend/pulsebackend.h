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

#ifndef PULSEBACKEND_H
#define PULSEBACKEND_H

#include "audiobackend.h"

struct pa_threaded_mainloop;
struct pa_context;
struct pa_stream;

/**
 * PulseAudio backend (libpulse). Works with a real PulseAudio daemon and
 * with PipeWire's pulse server (pipewire-pulse).
 *
 * Runs a pa_threaded_mainloop; the callbacks update m_raw with the
 * mainloop lock held and post a sync event to the GUI thread.
 *
 * Object ids are the PulseAudio index with the object type in the upper
 * bits, because sinks, sources, sink inputs, ... have separate index spaces.
 */
class PulseBackend : public AudioBackend
{
	TQ_OBJECT
public:
	enum IdType { SinkId = 1, SourceId = 2, SinkInputId = 3, SourceOutputId = 4, CardId = 5 };
	static uint32_t makeId(IdType type, uint32_t index) { return ((uint32_t)type << 28) | (index & 0x0fffffffu); }
	static uint32_t indexOf(uint32_t id) { return id & 0x0fffffffu; }
	static IdType typeOf(uint32_t id) { return (IdType)(id >> 28); }

	PulseBackend(TQObject *parent = 0);
	virtual ~PulseBackend();

	virtual Type type() const { return Pulse; }
	virtual TQString backendName() const { return "PulseAudio"; }
	virtual bool start();

	virtual void setPeaksEnabled(bool enabled);

	virtual void setDeviceVolume(uint32_t id, const std::vector<float> &volumes);
	virtual void setDeviceMute(uint32_t id, bool mute);
	virtual void setStreamVolume(uint32_t id, const std::vector<float> &volumes);
	virtual void setStreamMute(uint32_t id, bool mute);
	virtual void setDefaultDevice(uint32_t id);
	virtual void moveStream(uint32_t streamId, uint32_t deviceId);
	virtual void setDevicePort(uint32_t id, const std::string &port);
	virtual void setCardProfile(uint32_t id, const std::string &profile);

	/* used by the C callbacks, which run with the mainloop lock held */
	struct Private;
	Private *d;
	Audio::Model &rawModel() { return m_raw; }
	std::map<uint32_t, float> &rawPeaks() { return m_rawPeaks; }
	void requestSync() { scheduleSync(); }

protected:
	virtual void lock();
	virtual void unlock();
	virtual void afterSync();

private slots:
	void reconnect();

private:
	void connectContext();
	void updatePeakStreams();
};

#endif
