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

#ifndef PWBACKEND_H
#define PWBACKEND_H

#include "audiobackend.h"

/**
 * Native PipeWire backend (libpipewire-0.3).
 *
 * Runs a pw_thread_loop and watches the registry: audio nodes become
 * devices and streams, Audio/Device objects become cards (profiles and
 * routes, i.e. ports), links tell which device a stream uses and the
 * "default" metadata gives the default devices. Volumes of hardware
 * devices are set on the device's active route, like pipewire-pulse does,
 * so they are saved by the session manager; everything else is set on the
 * node's Props. Streams are moved and defaults changed through the
 * metadata, which the session manager (WirePlumber) acts on.
 *
 * Object ids are PipeWire global ids.
 */
class PipeWireBackend : public AudioBackend
{
	TQ_OBJECT
public:
	PipeWireBackend(TQObject *parent = 0);
	virtual ~PipeWireBackend();

	virtual Type type() const { return PipeWire; }
	virtual TQString backendName() const { return "PipeWire"; }
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

	/* used by the C callbacks, which run with the loop lock held */
	struct Private;
	Private *d;
	Audio::Model &rawModel() { return m_raw; }
	std::map<uint32_t, float> &rawPeaks() { return m_rawPeaks; }
	void requestSync() { scheduleSync(); }
	bool peaksWanted() const { return m_peaksEnabled; }

protected:
	virtual void lock();
	virtual void unlock();
	virtual void afterSync();

private slots:
	void reconnect();

private:
	bool connectCore();
	void disconnectCore();
	void setNodeVolume(uint32_t id, const std::vector<float> *volumes, int mute);
	void updatePeakStreams();
};

#endif
