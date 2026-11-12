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

#include "mixertray.h"
#include "traypopup.h"
#include "channelstrip.h"
#include "mixersettings.h"
#include "backend/audiobackend.h"

#include <tqimage.h>
#include <tqstylesheet.h>
#include <tqtimer.h>
#include <tqtooltip.h>

#include <tdeaboutdata.h>
#include <tdeaction.h>
#include <tdeapplication.h>
#include <tdeconfig.h>
#include <kglobalaccel.h>
#include <tdeglobal.h>
#include <tdelocale.h>
#include <tdemessagebox.h>
#include <tdepopupmenu.h>
#include <khelpmenu.h>
#include <kiconeffect.h>
#include <kiconloader.h>
#include <kkeydialog.h>
#include <krun.h>

using Audio::q;

static const char *TRAY_RC = "tde-mixer-trayrc";

MixerTray::MixerTray(TQWidget *parent, const char *name)
	: KSystemTray(parent, name)
{
	MixerSettings settings = MixerSettings::load();
	m_backend = AudioBackend::create(AudioBackend::typeFromString(settings.backend), this);

	m_popup = new TrayPopup(m_backend, 0, "TrayPopup");
	m_popup->setMaxVolume(settings.maxVolume());
	connect(m_popup, TQ_SIGNAL(openMixer()), TQ_SLOT(openMixer()));

	TDEAction *open = new TDEAction(i18n("Open &Mixer"), "tde-mixer", 0,
	                                this, TQ_SLOT(openMixer()), actionCollection(), "open_mixer");
	TDEAction *configure = new TDEAction(i18n("&Configure Volume Control..."), "configure", 0,
	                                     this, TQ_SLOT(openSettings()), actionCollection(), "configure");
	TDEAction *shortcuts = new TDEAction(i18n("Configure &Shortcuts..."), "configure_shortcuts", 0,
	                                     this, TQ_SLOT(slotConfigureShortcuts()), actionCollection(),
	                                     "configure_shortcuts");
	m_muteAction = new TDEToggleAction(i18n("M&ute"), "tde-mixer-muted", 0,
	                                   this, TQ_SLOT(slotMute()), actionCollection(), "mute");
	m_autostart = new TDEToggleAction(i18n("&Start Automatically at Login"), 0,
	                                  this, TQ_SLOT(slotAutostart()), actionCollection(), "autostart");

	TDEPopupMenu *menu = contextMenu();
	open->plug(menu);
	menu->insertSeparator();
	m_deviceMenu = new TDEPopupMenu(menu);
	m_deviceMenu->setCheckable(true);
	menu->insertItem(SmallIconSet("tde-mixer-speaker"), i18n("&Output Device"), m_deviceMenu);
	m_muteAction->plug(menu);
	menu->insertSeparator();
	configure->plug(menu);
	shortcuts->plug(menu);
	m_autostart->plug(menu);

	m_helpMenu = new KHelpMenu(this, TDEGlobal::instance()->aboutData(), false);
	TDEPopupMenu *help = new TDEPopupMenu(menu);
	help->insertItem(SmallIconSet("contents"), i18n("Volume Mixer &Handbook"), this, TQ_SLOT(slotHelp()));
	help->insertSeparator();
	help->insertItem(SmallIconSet("tde-mixer"), i18n("&About Volume Control"),
	                 m_helpMenu, TQ_SLOT(aboutApplication()));
	help->insertItem(SmallIconSet("about_tde"), i18n("About &TDE"), m_helpMenu, TQ_SLOT(aboutKDE()));
	menu->insertItem(SmallIconSet("help"), i18n("&Help"), help);

	connect(m_deviceMenu, TQ_SIGNAL(activated(int)), TQ_SLOT(slotDeviceSelected(int)));

	/* the multimedia keys; configurable, also in the Control Center's
	 * keyboard shortcuts module */
	m_accel = new TDEGlobalAccel(this);
	m_accel->insert("Volume Up", i18n("Increase Volume"), TQString::null,
	                TDEShortcut("XF86AudioRaiseVolume"), TDEShortcut("XF86AudioRaiseVolume"),
	                this, TQ_SLOT(volumeUp()));
	m_accel->insert("Volume Down", i18n("Decrease Volume"), TQString::null,
	                TDEShortcut("XF86AudioLowerVolume"), TDEShortcut("XF86AudioLowerVolume"),
	                this, TQ_SLOT(volumeDown()));
	m_accel->insert("Mute", i18n("Mute"), TQString::null,
	                TDEShortcut("XF86AudioMute"), TDEShortcut("XF86AudioMute"),
	                this, TQ_SLOT(toggleMute()));
	m_accel->insert("Mute Microphone", i18n("Mute Microphone"), TQString::null,
	                TDEShortcut("XF86AudioMicMute"), TDEShortcut("XF86AudioMicMute"),
	                this, TQ_SLOT(toggleMicMute()));
	m_accel->readSettings();
	m_accel->updateConnections();

	connect(m_backend, TQ_SIGNAL(changed()), TQ_SLOT(updateState()));
	updateState();

	TQTimer::singleShot(3000, this, TQ_SLOT(checkKMix()));
}

MixerTray::~MixerTray()
{
	delete m_popup;
}

float MixerTray::step() const
{
	return MixerSettings::load().volumeStep / 100.0f;
}

float MixerTray::maxLevel() const
{
	return MixerSettings::load().maxVolume() / 100.0f;
}

TQPixmap MixerTray::statePixmap() const
{
	TQPixmap pix = KSystemTray::loadIcon(m_iconName);
	if (!m_backend->isConnected()) {
		TQImage img = pix.convertToImage();
		TDEIconEffect::toGray(img, 1.0);
		TDEIconEffect::semiTransparent(img);
		pix.convertFromImage(img);
	}
	return pix;
}

void MixerTray::updateState()
{
	const Audio::Device *def = m_backend->defaultDevice(Audio::Sink);
	TQString tip;
	TQString icon;
	if (!m_backend->isConnected()) {
		icon = "tde-mixer-muted";
		tip = i18n("<b>Volume</b><br>Not connected to the sound server");
	} else if (!def) {
		icon = "tde-mixer-muted";
		tip = i18n("<b>Volume</b><br>No output device");
	} else {
		float lvl = Audio::level(def->volumes);
		icon = ChannelStrip::volumeIconName(lvl, def->muted);
		if (def->muted)
			tip = i18n("<b>Volume: muted</b>");
		else
			tip = i18n("<b>Volume: %1 %</b>").arg((int)(lvl * 100 + 0.5f));
		tip += "<br>" + TQStyleSheet::escape(q(def->description));
		int apps = m_backend->streams(Audio::Playback).size();
		if (apps > 0)
			tip += "<br>" + i18n("One application is playing sound",
			                     "%n applications are playing sound", apps);
	}

	if (icon != m_iconName || !pixmap()) {
		m_iconName = icon;
		setPixmap(statePixmap());
	}
	TQToolTip::remove(this);
	TQToolTip::add(this, tip);
	m_muteAction->setChecked(def && def->muted);
	m_muteAction->setEnabled(def != 0);
}

void MixerTray::contextMenuAboutToShow(TDEPopupMenu *)
{
	m_deviceMenu->clear();
	m_menuDevices.clear();
	const Audio::Device *def = m_backend->defaultDevice(Audio::Sink);
	std::vector<const Audio::Device *> devs = m_backend->devices(Audio::Sink);
	for (size_t i = 0; i < devs.size(); ++i) {
		m_deviceMenu->insertItem(q(devs[i]->description), (int)i);
		m_deviceMenu->setItemChecked((int)i, devs[i] == def);
		m_menuDevices.push_back(devs[i]->id);
	}
	if (devs.empty()) {
		m_deviceMenu->insertItem(i18n("No output devices"), 0);
		m_deviceMenu->setItemEnabled(0, false);
	}
	m_autostart->setChecked(MixerSettings::trayAutostartEnabled());
}

void MixerTray::mousePressEvent(TQMouseEvent *e)
{
	if (e->button() == TQt::LeftButton) {
		if (m_popup->isVisible()) {
			m_popup->hide();
		} else {
			m_popup->setMaxVolume(MixerSettings::load().maxVolume());
			/* mapToGlobal() is unreliable for an icon embedded in the
			 * tray; the click position is not */
			TQPoint p = e->globalPos() - e->pos();
			m_popup->popupNear(TQRect(p, size()));
		}
	} else if (e->button() == TQt::MidButton) {
		toggleMute();
	} else {
		KSystemTray::mousePressEvent(e);
	}
}

void MixerTray::wheelEvent(TQWheelEvent *e)
{
	if (e->delta() > 0)
		volumeUp();
	else
		volumeDown();
	e->accept();
}

void MixerTray::volumeUp()
{
	m_backend->changeDefaultVolume(Audio::Sink, step(), maxLevel());
}

void MixerTray::volumeDown()
{
	m_backend->changeDefaultVolume(Audio::Sink, -step(), maxLevel());
}

void MixerTray::toggleMute()
{
	m_backend->toggleDefaultMute(Audio::Sink);
}

void MixerTray::toggleMicMute()
{
	m_backend->toggleDefaultMute(Audio::Source);
}

void MixerTray::slotMute()
{
	const Audio::Device *def = m_backend->defaultDevice(Audio::Sink);
	if (def)
		m_backend->setDeviceMute(def->id, m_muteAction->isChecked());
}

void MixerTray::slotDeviceSelected(int id)
{
	if (id >= 0 && id < (int)m_menuDevices.size())
		m_backend->setDefaultDevice(m_menuDevices[id]);
}

void MixerTray::openMixer()
{
	KRun::runCommand("tde-mixer", "tde-mixer", "tde-mixer");
}

void MixerTray::openSettings()
{
	KRun::runCommand("tdecmshell mixer", "tdecmshell", "tde-mixer");
}

void MixerTray::slotHelp()
{
	kapp->invokeHelp("tray", "tde-mixer");
}

void MixerTray::slotAutostart()
{
	MixerSettings::setTrayAutostartEnabled(m_autostart->isChecked());
}

void MixerTray::slotConfigureShortcuts()
{
	KKeyDialog::configure(m_accel, 0, true);
	m_accel->updateConnections();
}

void MixerTray::checkKMix()
{
	/* ask once whether KMix's own tray icon should be turned off */
	TDEConfig config(TRAY_RC, false, false);
	config.setGroup("General");
	if (config.readBoolEntry("KMixChecked", false))
		return;
	if (!MixerSettings::load().replaceKMix || !MixerSettings::kmixAutostarts())
		return;
	config.writeEntry("KMixChecked", true);
	config.sync();

	int answer = KMessageBox::questionYesNo(0,
		i18n("<p>KMix, the old volume control, also starts at login and shows its own "
		     "icon in the system tray.</p>"
		     "<p>Turn KMix off, so that only this volume control is shown? KMix stays "
		     "installed and can be started again from the menu.</p>"),
		i18n("Volume Control"), KGuiItem(i18n("Turn KMix Off")), KGuiItem(i18n("Keep KMix")));
	if (answer == KMessageBox::Yes)
		MixerSettings::disableKMix();
}

#include "mixertray.moc"
