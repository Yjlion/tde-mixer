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

#include "mixertray.h"

#include <tdeaboutdata.h>
#include <tdecmdlineargs.h>
#include <tdelocale.h>
#include <kuniqueapplication.h>

static const char description[] = I18N_NOOP("Volume control in the system tray, for PipeWire and PulseAudio");

int main(int argc, char **argv)
{
	TDEAboutData about("tde-mixer-tray", I18N_NOOP("Volume Control"), TDE_MIXER_VERSION, description,
	                   TDEAboutData::License_GPL, "(C) 2026 tde-mixer developers", 0,
	                   "https://github.com/Yjlion/tde-mixer");
	about.addAuthor("tde-mixer developers", 0, 0, "https://github.com/Yjlion/tde-mixer");

	TDECmdLineArgs::init(argc, argv, &about);
	KUniqueApplication::addCmdLineOptions();

	if (!KUniqueApplication::start())
		return 0;

	KUniqueApplication app;
	app.disableSessionManagement();

	MixerTray *tray = new MixerTray();
	app.setMainWidget(tray);
	tray->show();

	return app.exec();
}
