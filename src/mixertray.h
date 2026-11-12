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

#ifndef MIXERTRAY_H
#define MIXERTRAY_H

#include <stdint.h>

#include <vector>

#include <ksystemtray.h>

class AudioBackend;
class TrayPopup;
class KHelpMenu;
class TDEGlobalAccel;
class TDEPopupMenu;
class TDEToggleAction;
class TDEAction;

/**
 * Volume control in kicker's system tray, a replacement for KMix's dock
 * icon: shows the default output's volume, left click opens a popup with
 * the master and per-application volumes, middle click mutes, the mouse
 * wheel changes the volume. Also handles the volume keys of the keyboard.
 */
class MixerTray : public KSystemTray
{
	TQ_OBJECT
public:
	MixerTray(TQWidget *parent = 0, const char *name = 0);
	virtual ~MixerTray();

public slots:
	void updateState();
	void openMixer();
	void openSettings();
	void volumeUp();
	void volumeDown();
	void toggleMute();
	void toggleMicMute();

protected:
	virtual void mousePressEvent(TQMouseEvent *e);
	virtual void wheelEvent(TQWheelEvent *e);
	virtual void contextMenuAboutToShow(TDEPopupMenu *menu);

private slots:
	void slotDeviceSelected(int id);
	void slotHelp();
	void slotAutostart();
	void slotMute();
	void slotConfigureShortcuts();
	void checkKMix();

private:
	TQPixmap statePixmap() const;
	float step() const;
	float maxLevel() const;

	AudioBackend *m_backend;
	TrayPopup *m_popup;
	TDEGlobalAccel *m_accel;
	KHelpMenu *m_helpMenu;
	TDEPopupMenu *m_deviceMenu;
	TDEToggleAction *m_muteAction;
	TDEToggleAction *m_autostart;
	TQString m_iconName;
	std::vector<uint32_t> m_menuDevices;
};

#endif
