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

#ifndef MIXERSETTINGS_H
#define MIXERSETTINGS_H

#include <tqstring.h>

/**
 * Settings shared by the mixer, the Control Center module and the tray.
 *
 * tde-mixerrc [General]: Backend, VolumeStep, AllowOverAmplify, ShowPeaks,
 * ShowVirtualDevices.
 * tde-mixer-trayrc [General]: Autostart (read by the autostart condition),
 * KMixChecked.
 */
struct MixerSettings
{
	TQString backend;        // auto, pipewire or pulse
	int volumeStep;          // percent, for the tray wheel and volume keys
	bool allowOverAmplify;   // sliders go up to 150 %
	bool showPeaks;          // level meters
	bool trayAutostart;
	bool replaceKMix;        // turn KMix's autostart off when the tray starts

	MixerSettings();
	static MixerSettings load();
	void save() const;

	/** highest volume a slider allows, in percent */
	int maxVolume() const { return allowOverAmplify ? 150 : 100; }

	static bool trayAutostartEnabled();
	static void setTrayAutostartEnabled(bool enable);

	/** KMix is installed and starts at login */
	static bool kmixAutostarts();
	/** turns KMix's autostart off and quits a running KMix */
	static void disableKMix();
};

#endif
