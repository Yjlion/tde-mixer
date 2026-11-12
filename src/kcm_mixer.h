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

#ifndef KCM_MIXER_H
#define KCM_MIXER_H

#include <tdecmodule.h>

class MixerWidget;
class SettingsPage;

class KCMMixer : public TDECModule
{
	TQ_OBJECT
public:
	KCMMixer(TQWidget *parent = 0, const char *name = 0, const TQStringList &args = TQStringList());
	virtual ~KCMMixer();

	virtual void load();
	virtual void save();
	virtual void defaults();
	virtual TQString quickHelp() const;

private slots:
	void slotChanged();

private:
	MixerWidget *m_mixer;
	SettingsPage *m_settings;
};

#endif
