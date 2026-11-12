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

/*
 * Drives one backend operation from the command line, so that
 * run-backend-tests.sh can check the result with pactl / pw-dump:
 *
 *   backendtest <pipewire|pulse> <operation> [arguments]
 *
 *   stream-volume <app name> <percent>
 *   stream-mute <app name> <0|1>
 *   move <app name> <device name>
 *   device-volume <device name> <percent>
 *   device-mute <device name> <0|1>
 *   default <device name>
 *   peaks                       prints the highest level seen in 2 s per object
 *   wait-reconnect              exits 0 once the server is back after a restart
 */

#include "backend/audiobackend.h"
#include "listing.h"

#include <stdio.h>
#include <string.h>

#include <tqapplication.h>
#include <tqdatetime.h>

#include <tdeaboutdata.h>
#include <tdeinstance.h>

static const Audio::Stream *findStream(AudioBackend *b, const char *app)
{
	const std::map<uint32_t, Audio::Stream> &s = b->model().streams;
	for (std::map<uint32_t, Audio::Stream>::const_iterator it = s.begin(); it != s.end(); ++it)
		if (it->second.appName == app)
			return &it->second;
	fprintf(stderr, "no stream of %s\n", app);
	return 0;
}

static const Audio::Device *findDevice(AudioBackend *b, const char *name)
{
	const std::map<uint32_t, Audio::Device> &d = b->model().devices;
	for (std::map<uint32_t, Audio::Device>::const_iterator it = d.begin(); it != d.end(); ++it)
		if (it->second.name == name)
			return &it->second;
	fprintf(stderr, "no device %s\n", name);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: backendtest <pipewire|pulse> <operation> [arguments]\n");
		return 64;
	}
	TQApplication app(argc, argv, false);
	TDEAboutData about("backendtest", "backendtest", "1");
	TDEInstance instance(&about);

	AudioBackend::Type type = AudioBackend::typeFromString(argv[1]);
	AudioBackend *b = AudioBackend::create(type, &app);
	if (!b || b->type() != type) {
		fprintf(stderr, "backend %s not available\n", argv[1]);
		return 2;
	}
	if (!waitForModel(b)) {
		fprintf(stderr, "cannot connect\n");
		return 2;
	}

	const char *op = argv[2];
	const Audio::Stream *s;
	const Audio::Device *d;

	if (!strcmp(op, "stream-volume") && argc == 5) {
		if (!(s = findStream(b, argv[3]))) return 1;
		b->setStreamLevel(s->id, atoi(argv[4]) / 100.0f);
	} else if (!strcmp(op, "stream-mute") && argc == 5) {
		if (!(s = findStream(b, argv[3]))) return 1;
		b->setStreamMute(s->id, atoi(argv[4]) != 0);
	} else if (!strcmp(op, "move") && argc == 5) {
		if (!(s = findStream(b, argv[3])) || !(d = findDevice(b, argv[4]))) return 1;
		b->moveStream(s->id, d->id);
	} else if (!strcmp(op, "device-volume") && argc == 5) {
		if (!(d = findDevice(b, argv[3]))) return 1;
		b->setDeviceLevel(d->id, atoi(argv[4]) / 100.0f);
	} else if (!strcmp(op, "device-mute") && argc == 5) {
		if (!(d = findDevice(b, argv[3]))) return 1;
		b->setDeviceMute(d->id, atoi(argv[4]) != 0);
	} else if (!strcmp(op, "default") && argc == 4) {
		if (!(d = findDevice(b, argv[3]))) return 1;
		b->setDefaultDevice(d->id);
	} else if (!strcmp(op, "peaks")) {
		b->setPeaksEnabled(true);
		std::map<uint32_t, float> max;
		TQTime t;
		t.start();
		while (t.elapsed() < 2500) {
			app.processEvents(50);
			const std::map<uint32_t, Audio::Stream> &st = b->model().streams;
			/* peak() consumes the value: read it once */
			for (std::map<uint32_t, Audio::Stream>::const_iterator it = st.begin(); it != st.end(); ++it) {
				float v = b->peak(it->first);
				max[it->first] = TQMAX(max[it->first], v);
			}
			const std::map<uint32_t, Audio::Device> &dv = b->model().devices;
			for (std::map<uint32_t, Audio::Device>::const_iterator it = dv.begin(); it != dv.end(); ++it) {
				float v = b->peak(it->first);
				max[it->first] = TQMAX(max[it->first], v);
			}
		}
		for (std::map<uint32_t, float>::iterator it = max.begin(); it != max.end(); ++it) {
			if (!b->stream(it->first) && !b->device(it->first))
				continue;   /* gone meanwhile (the test players restart) */
			const Audio::Stream *ps = b->stream(it->first);
			const Audio::Device *pd = b->device(it->first);
			printf("%s %.3f\n", ps ? ps->appName.c_str() : pd ? pd->name.c_str() : "?", it->second);
		}
		return 0;
	} else if (!strcmp(op, "wait-reconnect")) {
		/* wait for the connection to drop and come back */
		TQTime t;
		t.start();
		bool dropped = false;
		while (t.elapsed() < 20000) {
			app.processEvents(50);
			if (!b->isConnected())
				dropped = true;
			else if (dropped) {
				processEventsFor(500);
				printf("reconnected, %u devices\n", (unsigned)b->model().devices.size());
				return 0;
			}
		}
		fprintf(stderr, dropped ? "did not reconnect\n" : "connection did not drop\n");
		return 1;
	} else {
		fprintf(stderr, "bad operation\n");
		return 64;
	}
	processEventsFor(700);
	return 0;
}
