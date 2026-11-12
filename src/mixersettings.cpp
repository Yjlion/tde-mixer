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

#include "mixersettings.h"

#include <tdeapplication.h>
#include <tdeconfig.h>
#include <tdeglobal.h>
#include <kstandarddirs.h>
#include <dcopclient.h>

static const char *MIXER_RC = "tde-mixerrc";
static const char *TRAY_RC = "tde-mixer-trayrc";

MixerSettings::MixerSettings()
	: backend("auto"), volumeStep(5), allowOverAmplify(false), showPeaks(true),
	  trayAutostart(true), replaceKMix(true)
{
}

MixerSettings MixerSettings::load()
{
	MixerSettings s;
	{
		TDEConfig config(MIXER_RC, true, false);
		config.setGroup("General");
		s.backend = config.readEntry("Backend", s.backend);
		s.volumeStep = config.readNumEntry("VolumeStep", s.volumeStep);
		if (s.volumeStep < 1 || s.volumeStep > 25)
			s.volumeStep = 5;
		s.allowOverAmplify = config.readBoolEntry("AllowOverAmplify", s.allowOverAmplify);
		s.showPeaks = config.readBoolEntry("ShowPeaks", s.showPeaks);
		s.replaceKMix = config.readBoolEntry("ReplaceKMix", s.replaceKMix);
	}
	s.trayAutostart = trayAutostartEnabled();
	return s;
}

void MixerSettings::save() const
{
	{
		TDEConfig config(MIXER_RC, false, false);
		config.setGroup("General");
		config.writeEntry("Backend", backend);
		config.writeEntry("VolumeStep", volumeStep);
		config.writeEntry("AllowOverAmplify", allowOverAmplify);
		config.writeEntry("ShowPeaks", showPeaks);
		config.writeEntry("ReplaceKMix", replaceKMix);
		config.sync();
	}
	setTrayAutostartEnabled(trayAutostart);
}

bool MixerSettings::trayAutostartEnabled()
{
	TDEConfig config(TRAY_RC, true, false);
	config.setGroup("General");
	/* matches the X-TDE-autostart-condition default in the autostart file */
	return config.readBoolEntry("Autostart", true);
}

void MixerSettings::setTrayAutostartEnabled(bool enable)
{
	TDEConfig config(TRAY_RC, false, false);
	config.setGroup("General");
	config.writeEntry("Autostart", enable);
	config.sync();
}

bool MixerSettings::kmixAutostarts()
{
	if (TDEStandardDirs::findExe("kmix").isEmpty())
		return false;
	/* registered by ksmserver only, not in every application */
	TDEGlobal::dirs()->addResourceType("autostart", "share/autostart");
	if (locate("autostart", "kmix.desktop").isEmpty())
		return false;
	TDEConfig config("kmixrc", true, false);
	config.setGroup("General");
	/* kmix.desktop: X-TDE-autostart-condition=kmixrc:General:Autostart:true */
	return config.readBoolEntry("Autostart", true);
}

void MixerSettings::disableKMix()
{
	{
		TDEConfig config("kmixrc", false, false);
		config.setGroup("General");
		config.writeEntry("Autostart", false);
		config.sync();
	}
	DCOPClient *dcop = kapp ? kapp->dcopClient() : 0;
	if (dcop && dcop->isApplicationRegistered("kmix"))
		dcop->send("kmix", "MainApplication-Interface", "quit()", TQByteArray());
}
