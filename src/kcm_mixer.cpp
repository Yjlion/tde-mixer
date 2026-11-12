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

#include "kcm_mixer.h"
#include "mixerwidget.h"
#include "mixersettings.h"
#include "settingspage.h"

#include <tqlayout.h>

#include <tdeaboutdata.h>
#include <kgenericfactory.h>
#include <tdelocale.h>

typedef KGenericFactory<KCMMixer, TQWidget> KCMMixerFactory;
K_EXPORT_COMPONENT_FACTORY(kcm_mixer, KCMMixerFactory("tde-mixer"))

KCMMixer::KCMMixer(TQWidget *parent, const char *name, const TQStringList &)
	: TDECModule(parent, name)
{
	TDEAboutData *about = new TDEAboutData("kcmmixer", I18N_NOOP("Volume Mixer"), TDE_MIXER_VERSION,
		I18N_NOOP("Per-application volume mixer for PipeWire and PulseAudio"),
		TDEAboutData::License_GPL, "(C) 2026 tde-mixer developers");
	about->addAuthor("tde-mixer developers", 0, 0, "https://github.com/Yjlion/tde-mixer");
	setAboutData(about);

	/* volume changes apply at once; Apply/Defaults are for the Settings tab */
	setButtons(Help | Apply | Default);
	setUseRootOnlyMsg(false);

	TQVBoxLayout *layout = new TQVBoxLayout(this, 0, 0);
	m_mixer = new MixerWidget(0, this);
	m_mixer->setMinimumSize(640, 480);
	layout->addWidget(m_mixer);

	m_settings = new SettingsPage(m_mixer->backend(), this);
	m_mixer->addPage(m_settings, i18n("&Settings"));
	connect(m_settings, TQ_SIGNAL(changed()), TQ_SLOT(slotChanged()));

	load();
}

KCMMixer::~KCMMixer()
{
}

void KCMMixer::load()
{
	m_settings->load();
	emit changed(false);
}

void KCMMixer::save()
{
	m_settings->save();
	m_mixer->applySettings(MixerSettings::load());
	emit changed(false);
}

void KCMMixer::defaults()
{
	m_settings->defaults();
}

void KCMMixer::slotChanged()
{
	emit changed(true);
}

TQString KCMMixer::quickHelp() const
{
	return i18n("<h1>Volume Mixer</h1>"
	            "<p>Here you can change the volume of each application that plays or "
	            "records sound, move an application to another output or input device, "
	            "and set the volume, port and default of each device.</p>"
	            "<p>The <b>Configuration</b> tab selects the profile of each sound card, "
	            "for example analog stereo, HDMI or a headset mode.</p>"
	            "<p>Volume changes take effect at once. The <b>Settings</b> tab "
	            "configures the mixer and its system tray icon; press <b>Apply</b> "
	            "to save them.</p>"
	            "<p>The mixer works with PipeWire and with PulseAudio.</p>");
}

#include "kcm_mixer.moc"
