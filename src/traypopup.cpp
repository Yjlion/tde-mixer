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

#include "traypopup.h"
#include "channelstrip.h"
#include "backend/audiobackend.h"

#include <algorithm>
#include <vector>

#include <tqapplication.h>
#include <tqdesktopwidget.h>
#include <tqlabel.h>
#include <tqlayout.h>

#include <kiconloader.h>
#include <tdelocale.h>
#include <kpushbutton.h>
#include <kseparator.h>

static const unsigned MaxApps = 10;

TrayPopup::TrayPopup(AudioBackend *backend, TQWidget *parent, const char *name)
	: TQFrame(parent, name, WType_Popup), m_backend(backend), m_maxVolume(100),
	  m_masterId(Audio::InvalidId), m_master(0)
{
	setFrameStyle(TQFrame::PopupPanel | TQFrame::Raised);
	setLineWidth(1);

	TQVBoxLayout *l = new TQVBoxLayout(this, 8, 4);
	TQLabel *title = new TQLabel(i18n("<b>Volume</b>"), this);
	l->addWidget(title);

	m_masterBox = new TQWidget(this);
	new TQVBoxLayout(m_masterBox, 0, 0);
	l->addWidget(m_masterBox);

	l->addWidget(new KSeparator(this));
	m_appsLabel = new TQLabel(i18n("<b>Applications</b>"), this);
	l->addWidget(m_appsLabel);
	m_noApps = new TQLabel(i18n("No application is playing sound."), this);
	m_noApps->setEnabled(false);
	l->addWidget(m_noApps);
	m_appsBox = new TQWidget(this);
	m_appsLayout = new TQVBoxLayout(m_appsBox, 0, 2);
	l->addWidget(m_appsBox);

	l->addWidget(new KSeparator(this));
	TQHBoxLayout *buttons = new TQHBoxLayout(l, 6);
	buttons->addStretch(1);
	KPushButton *mixer = new KPushButton(SmallIconSet("tde-mixer"), i18n("&Mixer..."), this);
	buttons->addWidget(mixer);
	connect(mixer, TQ_SIGNAL(clicked()), TQ_SLOT(slotOpenMixer()));

	connect(m_backend, TQ_SIGNAL(changed()), TQ_SLOT(sync()));
	sync();
}

void TrayPopup::setMaxVolume(int percent)
{
	m_maxVolume = percent;
	if (m_master)
		m_master->setMaxVolume(percent);
	for (std::map<uint32_t, ChannelStrip *>::iterator it = m_apps.begin(); it != m_apps.end(); ++it)
		it->second->setMaxVolume(percent);
}

void TrayPopup::sync()
{
	const Audio::Device *def = m_backend->defaultDevice(Audio::Sink);
	uint32_t id = def ? def->id : Audio::InvalidId;
	if (id != m_masterId) {
		delete m_master;
		m_master = 0;
		m_masterId = id;
		if (def) {
			m_master = new ChannelStrip(m_backend, id, ChannelStrip::DeviceStrip, true, m_masterBox);
			m_master->setMaxVolume(m_maxVolume);
			m_masterBox->layout()->add(m_master);
			m_master->show();
		}
	} else if (m_master) {
		m_master->updateFromModel();
	}

	std::vector<const Audio::Stream *> streams = m_backend->streams(Audio::Playback);
	std::vector<uint32_t> ids;
	for (size_t i = 0; i < streams.size() && ids.size() < MaxApps; ++i)
		ids.push_back(streams[i]->id);

	for (std::map<uint32_t, ChannelStrip *>::iterator it = m_apps.begin(); it != m_apps.end();) {
		if (std::find(ids.begin(), ids.end(), it->first) == ids.end()) {
			delete it->second;
			m_apps.erase(it++);
		} else {
			++it;
		}
	}
	int index = 0;
	for (size_t i = 0; i < ids.size(); ++i, ++index) {
		std::map<uint32_t, ChannelStrip *>::iterator it = m_apps.find(ids[i]);
		if (it != m_apps.end()) {
			it->second->updateFromModel();
			continue;
		}
		ChannelStrip *s = new ChannelStrip(m_backend, ids[i], ChannelStrip::StreamStrip, true, m_appsBox);
		s->setMaxVolume(m_maxVolume);
		m_appsLayout->insertWidget(index, s);
		s->show();
		m_apps[ids[i]] = s;
	}
	m_noApps->setShown(ids.empty());
	if (isVisible()) {
		layout()->activate();
		resize(TQMAX(minimumSizeHint().width(), 320), sizeHint().height());
	}
}

void TrayPopup::popupNear(const TQRect &anchor)
{
	sync();
	/* the final size, which adjustSize() does not give before the
	 * layout has run once */
	layout()->activate();
	resize(TQMAX(minimumSizeHint().width(), 320), sizeHint().height());
	TQDesktopWidget *desktop = TQApplication::desktop();
	TQRect screen = desktop->screenGeometry(desktop->screenNumber(anchor.center()));
	int x = anchor.center().x() - width() / 2;
	int y = anchor.top() - height() - 2;
	if (y < screen.top())               /* panel at the top */
		y = anchor.bottom() + 2;
	x = TQMAX(screen.left(), TQMIN(x, screen.right() - width()));
	y = TQMAX(screen.top(), TQMIN(y, screen.bottom() - height()));
	move(x, y);
	show();
}

void TrayPopup::slotOpenMixer()
{
	hide();
	emit openMixer();
}

#include "traypopup.moc"
