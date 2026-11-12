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

#include "listing.h"
#include "backend/audiobackend.h"

#include <stdio.h>

#include <tqapplication.h>
#include <tqdatetime.h>

using Audio::q;

void processEventsFor(int ms)
{
	TQTime t;
	t.start();
	while (t.elapsed() < ms)
		tqApp->processEvents(50);
}

bool waitForModel(AudioBackend *backend, int timeoutMs)
{
	TQTime t;
	t.start();
	while (!backend->isConnected() && t.elapsed() < timeoutMs)
		tqApp->processEvents(50);
	if (!backend->isConnected())
		return false;
	/* the info of the objects arrives after the connection */
	processEventsFor(500);
	return true;
}

static TQString volumeText(const std::vector<float> &volumes, const std::vector<std::string> &channels, bool muted)
{
	TQString s = TQString("%1 %").arg((int)(Audio::level(volumes) * 100 + 0.5f));
	if (volumes.size() > 1) {
		TQStringList parts;
		for (size_t i = 0; i < volumes.size(); ++i)
			parts << TQString("%1 %2 %").arg(i < channels.size() ? q(channels[i]) : TQString::number(i))
			                            .arg((int)(volumes[i] * 100 + 0.5f));
		s += " (" + parts.join(", ") + ")";
	}
	if (muted)
		s += " muted";
	return s;
}

static void printDevices(AudioBackend *b, Audio::DeviceKind kind, const char *title)
{
	printf("%s:\n", title);
	const Audio::Device *def = b->defaultDevice(kind);
	std::vector<const Audio::Device *> devs = b->devices(kind);
	for (size_t i = 0; i < devs.size(); ++i) {
		const Audio::Device *d = devs[i];
		printf("  %c %-10u %s [%s]\n", def == d ? '*' : ' ', d->id,
		       q(d->description).utf8().data(), d->name.c_str());
		printf("               volume: %s\n",
		       volumeText(d->volumes, d->channels, d->muted).utf8().data());
		if (!d->ports.empty())
			printf("               port: %s (%u ports)\n", d->activePort.c_str(), (unsigned)d->ports.size());
	}
}

static void printStreams(AudioBackend *b, Audio::StreamKind kind, const char *title)
{
	printf("%s:\n", title);
	std::vector<const Audio::Stream *> st = b->streams(kind);
	for (size_t i = 0; i < st.size(); ++i) {
		const Audio::Stream *s = st[i];
		const Audio::Device *d = b->device(s->deviceId);
		printf("    %-10u %s: %s\n", s->id, s->appName.c_str(), s->mediaName.c_str());
		printf("               device: %s, volume: %s%s\n",
		       d ? d->name.c_str() : "(none)",
		       volumeText(s->volumes, s->channels, s->muted).utf8().data(),
		       s->corked ? ", paused" : "");
	}
}

int runListing(AudioBackend *b)
{
	if (!waitForModel(b)) {
		fprintf(stderr, "tde-mixer: cannot connect to the sound server (%s backend)\n",
		        b->backendName().utf8().data());
		return 2;
	}
	printf("Backend: %s, server: %s\n", b->backendName().utf8().data(), b->model().serverName.c_str());
	printDevices(b, Audio::Sink, "Output devices");
	printDevices(b, Audio::Source, "Input devices");
	printStreams(b, Audio::Playback, "Playback");
	printStreams(b, Audio::Recording, "Recording");
	printf("Cards:\n");
	const std::map<uint32_t, Audio::Card> &cards = b->model().cards;
	for (std::map<uint32_t, Audio::Card>::const_iterator it = cards.begin(); it != cards.end(); ++it)
		printf("    %-10u %s, profile: %s (%u profiles)\n", it->first, it->second.description.c_str(),
		       it->second.activeProfile.c_str(), (unsigned)it->second.profiles.size());
	return 0;
}

int runSetVolume(AudioBackend *b, int percent)
{
	if (!waitForModel(b)) {
		fprintf(stderr, "tde-mixer: cannot connect to the sound server\n");
		return 2;
	}
	const Audio::Device *d = b->defaultDevice(Audio::Sink);
	if (!d) {
		fprintf(stderr, "tde-mixer: there is no output device\n");
		return 3;
	}
	uint32_t id = d->id;
	b->setDeviceLevel(id, percent / 100.0f);
	processEventsFor(500);
	d = b->device(id);
	if (d)
		printf("%s: %s\n", d->name.c_str(), volumeText(d->volumes, d->channels, d->muted).utf8().data());
	return 0;
}

int runToggleMute(AudioBackend *b)
{
	if (!waitForModel(b)) {
		fprintf(stderr, "tde-mixer: cannot connect to the sound server\n");
		return 2;
	}
	const Audio::Device *d = b->defaultDevice(Audio::Sink);
	if (!d) {
		fprintf(stderr, "tde-mixer: there is no output device\n");
		return 3;
	}
	uint32_t id = d->id;
	b->setDeviceMute(id, !d->muted);
	processEventsFor(500);
	d = b->device(id);
	if (d)
		printf("%s: %s\n", d->name.c_str(), volumeText(d->volumes, d->channels, d->muted).utf8().data());
	return 0;
}
