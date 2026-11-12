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

#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <tqwidget.h>

#include "mixersettings.h"

class AudioBackend;
class TQCheckBox;
class TQComboBox;
class TQLabel;
class KIntNumInput;

/** the preferences, shown in the Control Center module and in the mixer's Settings dialog */
class SettingsPage : public TQWidget
{
	TQ_OBJECT
public:
	SettingsPage(AudioBackend *backend, TQWidget *parent = 0, const char *name = 0);

	MixerSettings settings() const;
	void setSettings(const MixerSettings &s);

public slots:
	void load();
	void save();
	void defaults();

signals:
	void changed();

private slots:
	void slotChanged();

private:
	AudioBackend *m_backend;
	TQComboBox *m_backendCombo;
	TQStringList m_backendIds;
	TQLabel *m_backendInfo;
	KIntNumInput *m_step;
	TQCheckBox *m_overAmplify;
	TQCheckBox *m_peaks;
	TQCheckBox *m_autostart;
	TQCheckBox *m_replaceKMix;
};

#endif
