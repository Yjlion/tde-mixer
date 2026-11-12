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

#include "mainwindow.h"
#include "mixerwidget.h"
#include "mixersettings.h"
#include "settingspage.h"

#include <tqlayout.h>

#include <tdeaction.h>
#include <tdeapplication.h>
#include <kdialogbase.h>
#include <tdelocale.h>
#include <krun.h>
#include <kstdaction.h>

MainWindow::MainWindow(AudioBackend *backend)
	: TDEMainWindow(0, "MixerMainWindow")
{
	m_mixer = new MixerWidget(backend, this);
	setCentralWidget(m_mixer);

	KStdAction::quit(kapp, TQ_SLOT(quit()), actionCollection());
	KStdAction::preferences(this, TQ_SLOT(slotPreferences()), actionCollection());
	new TDEAction(i18n("Open in &Control Center"), "kcontrol", 0, this, TQ_SLOT(slotSystemSettings()),
	              actionCollection(), "open_kcontrol");

	setupGUI(Keys | Save | Create, "tde-mixerui.rc");
	setAutoSaveSettings();
	if (!initialGeometrySet())
		resize(760, 560);
}

void MainWindow::slotPreferences()
{
	KDialogBase dlg(KDialogBase::Plain, i18n("Configure Volume Mixer"),
	                KDialogBase::Ok | KDialogBase::Cancel | KDialogBase::Default | KDialogBase::Help,
	                KDialogBase::Ok, this, "settings", true, true);
	dlg.setHelp("settings", "tde-mixer");
	TQWidget *plain = dlg.plainPage();
	TQVBoxLayout *l = new TQVBoxLayout(plain, 0, 0);
	SettingsPage *page = new SettingsPage(m_mixer->backend(), plain);
	l->addWidget(page);
	connect(&dlg, TQ_SIGNAL(defaultClicked()), page, TQ_SLOT(defaults()));
	if (dlg.exec() == TQDialog::Accepted) {
		page->save();
		m_mixer->applySettings(MixerSettings::load());
	}
}

void MainWindow::slotSystemSettings()
{
	KRun::runCommand("tdecmshell mixer", "tdecmshell", "tde-mixer");
}

#include "mainwindow.moc"
