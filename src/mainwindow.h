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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <tdemainwindow.h>

class AudioBackend;
class MixerWidget;

class MainWindow : public TDEMainWindow
{
	TQ_OBJECT
public:
	MainWindow(AudioBackend *backend);
	MixerWidget *mixerWidget() const { return m_mixer; }

private slots:
	void slotPreferences();
	void slotSystemSettings();

private:
	MixerWidget *m_mixer;
};

#endif
