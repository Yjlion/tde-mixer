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

#ifndef TRAYPOPUP_H
#define TRAYPOPUP_H

#include <stdint.h>

#include <map>

#include <tqframe.h>

class AudioBackend;
class ChannelStrip;
class TQLabel;
class TQVBoxLayout;
class TQWidget;

/**
 * The popup of the tray icon: the default output's volume and one compact
 * row for each application playing sound.
 */
class TrayPopup : public TQFrame
{
	TQ_OBJECT
public:
	TrayPopup(AudioBackend *backend, TQWidget *parent = 0, const char *name = 0);

	/** shows the popup next to @p anchor (global coordinates of the tray icon) */
	void popupNear(const TQRect &anchor);
	void setMaxVolume(int percent);

public slots:
	void sync();

signals:
	void openMixer();

private slots:
	void slotOpenMixer();

private:
	AudioBackend *m_backend;
	int m_maxVolume;
	uint32_t m_masterId;
	ChannelStrip *m_master;
	TQWidget *m_masterBox;
	TQLabel *m_appsLabel;
	TQLabel *m_noApps;
	TQWidget *m_appsBox;
	TQVBoxLayout *m_appsLayout;
	std::map<uint32_t, ChannelStrip *> m_apps;
};

#endif
