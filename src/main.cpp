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

#include "mainwindow.h"
#include "mixerwidget.h"
#include "mixersettings.h"
#include "backend/audiobackend.h"
#include "listing.h"

#include <stdio.h>

#include <tdeaboutdata.h>
#include <tdeapplication.h>
#include <tdecmdlineargs.h>
#include <tdeinstance.h>

#include <tqapplication.h>
#include <tdelocale.h>

static const char description[] = I18N_NOOP("Per-application volume mixer for PipeWire and PulseAudio");

static TDECmdLineOptions options[] = {
	{ "page <page>", I18N_NOOP("Initial page: playback, recording, output, input or configuration"), "playback" },
	{ "backend <backend>", I18N_NOOP("Audio backend: auto, pipewire or pulse (default: from the settings)"), 0 },
	{ "list", I18N_NOOP("Print the devices, streams and cards and exit"), 0 },
	{ "volume <percent>", I18N_NOOP("Set the volume of the default output and exit"), 0 },
	{ "toggle-mute", I18N_NOOP("Mute or unmute the default output and exit"), 0 },
	TDECmdLineLastOption
};

static AudioBackend *createBackend(TDECmdLineArgs *args, TQObject *parent)
{
	TQString name = args->isSet("backend") ? TQString::fromLocal8Bit(args->getOption("backend"))
	                                        : MixerSettings::load().backend;
	AudioBackend *backend = AudioBackend::create(AudioBackend::typeFromString(name), parent);
	if (!backend)
		fprintf(stderr, "tde-mixer: no audio backend was built in\n");
	return backend;
}

int main(int argc, char **argv)
{
	TDEAboutData about("tde-mixer", I18N_NOOP("Volume Mixer"), TDE_MIXER_VERSION, description,
	                   TDEAboutData::License_GPL, "(C) 2026 tde-mixer developers", 0,
	                   "https://github.com/Yjlion/tde-mixer");
	about.addAuthor("tde-mixer developers", 0, 0, "https://github.com/Yjlion/tde-mixer");

	TDECmdLineArgs::init(argc, argv, &about);
	TDECmdLineArgs::addCmdLineOptions(options);
	TDECmdLineArgs *args = TDECmdLineArgs::parsedArgs();

	bool command = args->isSet("list") || args->isSet("volume") || args->isSet("toggle-mute");

	if (command) {
		/* the command line actions need neither an X display nor DCOP */
		int qargc = 1;
		char *qargv[] = { argv[0], 0 };
		TQApplication app(qargc, qargv, false);
		TDEInstance instance(&about);
		AudioBackend *backend = createBackend(args, &app);
		if (!backend)
			return 1;
		int rc;
		if (args->isSet("list"))
			rc = runListing(backend);
		else if (args->isSet("volume"))
			rc = runSetVolume(backend, TQString::fromLocal8Bit(args->getOption("volume")).toInt());
		else
			rc = runToggleMute(backend);
		args->clear();
		return rc;
	}

	TDEApplication app;
	AudioBackend *backend = createBackend(args, &app);
	if (!backend)
		return 1;

	MainWindow *win = new MainWindow(backend);
	app.setMainWidget(win);
	win->mixerWidget()->showPage(TQString::fromLocal8Bit(args->getOption("page")));
	args->clear();

	win->show();
	return app.exec();
}
