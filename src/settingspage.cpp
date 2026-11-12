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

#include "settingspage.h"
#include "backend/audiobackend.h"

#include <tqcheckbox.h>
#include <tqcombobox.h>
#include <tqgroupbox.h>
#include <tqlabel.h>
#include <tqlayout.h>
#include <tqwhatsthis.h>

#include <knuminput.h>
#include <tdelocale.h>

SettingsPage::SettingsPage(AudioBackend *backend, TQWidget *parent, const char *name)
	: TQWidget(parent, name), m_backend(backend)
{
	TQVBoxLayout *l = new TQVBoxLayout(this, 8, 8);

	/* sound server */
	TQGroupBox *server = new TQGroupBox(1, TQt::Horizontal, i18n("Sound Server"), this);
	TQWidget *row = new TQWidget(server);
	TQHBoxLayout *rl = new TQHBoxLayout(row, 0, 6);
	TQLabel *bl = new TQLabel(i18n("&Backend:"), row);
	m_backendCombo = new TQComboBox(false, row);
	bl->setBuddy(m_backendCombo);
	rl->addWidget(bl);
	rl->addWidget(m_backendCombo);
	rl->addStretch(1);

	m_backendIds << "auto";
	m_backendCombo->insertItem(i18n("Automatic (PipeWire if it is running)"));
	TQStringList compiled = AudioBackend::compiledBackends();
	if (compiled.contains("pipewire")) {
		m_backendIds << "pipewire";
		m_backendCombo->insertItem(i18n("PipeWire (native)"));
	}
	if (compiled.contains("pulse")) {
		m_backendIds << "pulse";
		m_backendCombo->insertItem(i18n("PulseAudio (also PipeWire through pipewire-pulse)"));
	}
	TQWhatsThis::add(m_backendCombo,
		i18n("<p>How the mixer talks to the sound server.</p>"
		     "<p><b>PipeWire</b> uses PipeWire directly. <b>PulseAudio</b> uses the "
		     "PulseAudio protocol, which works with a PulseAudio daemon and with "
		     "PipeWire's pulse server. <b>Automatic</b> uses PipeWire when it is "
		     "running and PulseAudio otherwise.</p>"
		     "<p>A change takes effect the next time the mixer, the module or the "
		     "tray icon is started.</p>"));

	m_backendInfo = new TQLabel(server);
	if (m_backend)
		m_backendInfo->setText(i18n("In use: %1").arg(m_backend->backendName()));

	/* volume */
	TQGroupBox *volume = new TQGroupBox(1, TQt::Horizontal, i18n("Volume"), this);
	m_step = new KIntNumInput(volume);
	m_step->setRange(1, 25, 1, true);
	m_step->setSuffix(i18n(" %"));
	m_step->setLabel(i18n("&Step for the volume keys and the mouse wheel:"), TQt::AlignLeft | TQt::AlignVCenter);
	m_overAmplify = new TQCheckBox(i18n("Allow volumes above 100 % (&amplification)"), volume);
	TQWhatsThis::add(m_overAmplify,
		i18n("Lets the sliders go up to 150 %. Sound above 100 % is amplified in "
		     "software and may be distorted."));
	m_peaks = new TQCheckBox(i18n("Show &level meters"), volume);

	/* tray */
	TQGroupBox *tray = new TQGroupBox(1, TQt::Horizontal, i18n("System Tray"), this);
	m_autostart = new TQCheckBox(i18n("&Start the volume control in the system tray at login"), tray);
	m_replaceKMix = new TQCheckBox(i18n("Offer to turn off &KMix's tray icon"), tray);
	TQWhatsThis::add(m_replaceKMix,
		i18n("When the tray icon starts and KMix is set to start at login, ask once "
		     "whether KMix should be turned off, so that there is only one volume "
		     "icon in the panel."));

	l->addWidget(server);
	l->addWidget(volume);
	l->addWidget(tray);
	l->addStretch(1);

	connect(m_backendCombo, TQ_SIGNAL(activated(int)), TQ_SLOT(slotChanged()));
	connect(m_step, TQ_SIGNAL(valueChanged(int)), TQ_SLOT(slotChanged()));
	connect(m_overAmplify, TQ_SIGNAL(toggled(bool)), TQ_SLOT(slotChanged()));
	connect(m_peaks, TQ_SIGNAL(toggled(bool)), TQ_SLOT(slotChanged()));
	connect(m_autostart, TQ_SIGNAL(toggled(bool)), TQ_SLOT(slotChanged()));
	connect(m_replaceKMix, TQ_SIGNAL(toggled(bool)), TQ_SLOT(slotChanged()));

	load();
}

void SettingsPage::setSettings(const MixerSettings &s)
{
	blockSignals(true);
	int idx = m_backendIds.findIndex(s.backend);
	m_backendCombo->setCurrentItem(idx < 0 ? 0 : idx);
	m_step->setValue(s.volumeStep);
	m_overAmplify->setChecked(s.allowOverAmplify);
	m_peaks->setChecked(s.showPeaks);
	m_autostart->setChecked(s.trayAutostart);
	m_replaceKMix->setChecked(s.replaceKMix);
	blockSignals(false);
}

MixerSettings SettingsPage::settings() const
{
	MixerSettings s;
	s.backend = m_backendIds[m_backendCombo->currentItem()];
	s.volumeStep = m_step->value();
	s.allowOverAmplify = m_overAmplify->isChecked();
	s.showPeaks = m_peaks->isChecked();
	s.trayAutostart = m_autostart->isChecked();
	s.replaceKMix = m_replaceKMix->isChecked();
	return s;
}

void SettingsPage::load()
{
	setSettings(MixerSettings::load());
}

void SettingsPage::save()
{
	settings().save();
}

void SettingsPage::defaults()
{
	setSettings(MixerSettings());
	emit changed();
}

void SettingsPage::slotChanged()
{
	emit changed();
}

#include "settingspage.moc"
