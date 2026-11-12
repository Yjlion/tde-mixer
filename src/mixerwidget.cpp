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

#include "mixerwidget.h"
#include "mixersettings.h"
#include "backend/audiobackend.h"

#include <algorithm>

#include <tqcombobox.h>
#include <tqlabel.h>
#include <tqlayout.h>
#include <tqtabwidget.h>
#include <tqtimer.h>

#include <tdeglobal.h>
#include <kiconloader.h>
#include <tdelocale.h>

using Audio::q;

/* ------------------------------------------------------------------ */

StripList::StripList(const TQString &emptyText, TQWidget *parent, const char *name)
	: TQScrollView(parent, name)
{
	setHScrollBarMode(AlwaysOff);
	setResizePolicy(Manual);
	setFrameStyle(TQFrame::NoFrame);
	viewport()->setBackgroundMode(PaletteBackground);

	m_box = new TQWidget(viewport());
	m_box->setBackgroundMode(PaletteBackground);
	m_layout = new TQVBoxLayout(m_box, 6, 6);
	m_empty = new TQLabel(emptyText, m_box);
	m_empty->setAlignment(TQt::AlignCenter | TQt::WordBreak);
	m_empty->setEnabled(false);
	m_empty->setMinimumHeight(80);
	m_layout->addWidget(m_empty);
	m_layout->addStretch(1);
	addChild(m_box);
}

void StripList::insertRow(TQWidget *w, int index)
{
	/* index 0 is the empty label */
	m_layout->insertWidget(index + 1, w);
	w->show();
	viewportResizeEvent(0);
}

void StripList::updateEmpty(bool empty)
{
	m_empty->setShown(empty);
	viewportResizeEvent(0);
}

void StripList::viewportResizeEvent(TQResizeEvent *e)
{
	if (e)
		TQScrollView::viewportResizeEvent(e);
	/* the rows always fill the width; the height follows the content */
	m_layout->activate();
	int h = TQMAX(m_box->sizeHint().height(), visibleHeight());
	m_box->resize(visibleWidth(), h);
	resizeContents(visibleWidth(), h);
}

/* ------------------------------------------------------------------ */

CardRow::CardRow(AudioBackend *backend, uint32_t id, TQWidget *parent)
	: TQFrame(parent), m_backend(backend), m_id(id)
{
	setFrameStyle(TQFrame::StyledPanel | TQFrame::Raised);
	m_icon = new TQLabel(this);
	m_icon->setPixmap(DesktopIcon("tde-mixer-card", 48));
	m_title = new TQLabel(this);
	TQFont f = m_title->font();
	f.setBold(true);
	m_title->setFont(f);
	m_subtitle = new TQLabel(this);
	m_profiles = new TQComboBox(false, this);
	TQLabel *pl = new TQLabel(i18n("&Profile:"), this);
	pl->setBuddy(m_profiles);

	TQHBoxLayout *l = new TQHBoxLayout(this, 6, 8);
	l->addWidget(m_icon, 0, TQt::AlignTop);
	TQVBoxLayout *v = new TQVBoxLayout(l, 4);
	v->addWidget(m_title);
	v->addWidget(m_subtitle);
	TQHBoxLayout *h = new TQHBoxLayout(v, 6);
	h->addWidget(pl);
	h->addWidget(m_profiles, 1);

	connect(m_profiles, TQ_SIGNAL(activated(int)), TQ_SLOT(slotProfileSelected(int)));
	updateFromModel();
}

void CardRow::updateFromModel()
{
	const Audio::Card *c = m_backend->card(m_id);
	if (!c)
		return;
	m_title->setText(q(c->description));
	m_subtitle->setText(q(c->name));

	TQStringList texts;
	std::vector<std::string> names;
	int current = -1;
	for (size_t i = 0; i < c->profiles.size(); ++i) {
		TQString t = q(c->profiles[i].description);
		if (!c->profiles[i].available)
			t = i18n("%1 (unavailable)").arg(t);
		texts << t;
		names.push_back(c->profiles[i].name);
		if (c->profiles[i].name == c->activeProfile)
			current = (int)i;
	}
	if (names != m_names || texts != m_texts) {
		m_profiles->clear();
		m_profiles->insertStringList(texts);
		m_names = names;
		m_texts = texts;
	}
	if (current >= 0 && m_profiles->currentItem() != current)
		m_profiles->setCurrentItem(current);
}

void CardRow::slotProfileSelected(int index)
{
	if (index >= 0 && index < (int)m_names.size())
		m_backend->setCardProfile(m_id, m_names[index]);
}

/* ------------------------------------------------------------------ */

MixerWidget::MixerWidget(AudioBackend *backend, TQWidget *parent, const char *name)
	: TQWidget(parent, name), m_backend(backend), m_ownBackend(false), m_maxVolume(100), m_showPeaks(true)
{
	MixerSettings settings = MixerSettings::load();
	if (!m_backend) {
		m_backend = AudioBackend::create(AudioBackend::typeFromString(settings.backend), this);
		m_ownBackend = true;
	}

	TQVBoxLayout *l = new TQVBoxLayout(this, 0, 6);
	m_tabs = new TQTabWidget(this);
	l->addWidget(m_tabs, 1);

	m_lists[PlaybackPage] = new StripList(i18n("No application is playing sound."), m_tabs);
	m_lists[RecordingPage] = new StripList(i18n("No application is recording sound."), m_tabs);
	m_lists[OutputPage] = new StripList(i18n("No output devices were found."), m_tabs);
	m_lists[InputPage] = new StripList(i18n("No input devices were found."), m_tabs);
	m_cardList = new StripList(i18n("No sound cards were found."), m_tabs);

	m_tabs->addTab(m_lists[PlaybackPage], SmallIconSet("tde-mixer-application"), i18n("&Playback"));
	m_tabs->addTab(m_lists[RecordingPage], SmallIconSet("tde-mixer-microphone"), i18n("&Recording"));
	m_tabs->addTab(m_lists[OutputPage], SmallIconSet("tde-mixer-speaker"), i18n("&Output Devices"));
	m_tabs->addTab(m_lists[InputPage], SmallIconSet("tde-mixer-microphone"), i18n("&Input Devices"));
	m_tabs->addTab(m_cardList, SmallIconSet("tde-mixer-card"), i18n("&Configuration"));

	m_status = new TQLabel(this);
	l->addWidget(m_status);

	m_peakTimer = new TQTimer(this);
	connect(m_peakTimer, TQ_SIGNAL(timeout()), TQ_SLOT(updatePeaks()));
	connect(m_tabs, TQ_SIGNAL(currentChanged(TQWidget *)), TQ_SLOT(slotCurrentChanged(TQWidget *)));

	if (m_backend)
		connect(m_backend, TQ_SIGNAL(changed()), TQ_SLOT(sync()));

	applySettings(settings);
	sync();
}

MixerWidget::~MixerWidget()
{
	if (m_backend && !m_ownBackend)
		m_backend->setPeaksEnabled(false);
}

void MixerWidget::applySettings(const MixerSettings &settings)
{
	m_maxVolume = settings.maxVolume();
	m_showPeaks = settings.showPeaks;
	for (int p = 0; p < 4; ++p)
		for (StripMap::iterator it = m_strips[p].begin(); it != m_strips[p].end(); ++it) {
			it->second->setMaxVolume(m_maxVolume);
			it->second->setPeaksVisible(m_showPeaks);
		}
	if (m_backend)
		m_backend->setPeaksEnabled(m_showPeaks);
	if (m_showPeaks)
		m_peakTimer->start(50);
	else
		m_peakTimer->stop();
}

void MixerWidget::showPage(const TQString &page)
{
	TQString p = page.lower();
	if (p == "recording")
		showPage(RecordingPage);
	else if (p == "output" || p == "outputs")
		showPage(OutputPage);
	else if (p == "input" || p == "inputs")
		showPage(InputPage);
	else if (p == "configuration" || p == "cards")
		showPage(CardsPage);
	else
		showPage(PlaybackPage);
}

void MixerWidget::showPage(Page page)
{
	m_tabs->setCurrentPage((int)page);
}

void MixerWidget::addPage(TQWidget *page, const TQString &label)
{
	m_tabs->addTab(page, SmallIconSet("configure"), label);
}

void MixerWidget::syncStrips(StripList *list, StripMap &strips, const std::vector<uint32_t> &ids,
                             ChannelStrip::Type type)
{
	/* remove the rows of objects that are gone */
	for (StripMap::iterator it = strips.begin(); it != strips.end();) {
		if (std::find(ids.begin(), ids.end(), it->first) == ids.end()) {
			delete it->second;
			strips.erase(it++);
		} else {
			++it;
		}
	}
	/* add new rows in id order (the order objects appeared in), update the rest */
	int index = 0;
	for (size_t i = 0; i < ids.size(); ++i, ++index) {
		StripMap::iterator it = strips.find(ids[i]);
		if (it != strips.end()) {
			it->second->updateFromModel();
			continue;
		}
		ChannelStrip *s = new ChannelStrip(m_backend, ids[i], type, false, list->container());
		s->setMaxVolume(m_maxVolume);
		s->setPeaksVisible(m_showPeaks);
		strips[ids[i]] = s;
		list->insertRow(s, index);
	}
	list->updateEmpty(ids.empty());
}

void MixerWidget::sync()
{
	if (!m_backend) {
		m_status->setText(i18n("This program was built without any audio backend."));
		return;
	}

	std::vector<uint32_t> ids[4];
	std::vector<const Audio::Stream *> play = m_backend->streams(Audio::Playback);
	for (size_t i = 0; i < play.size(); ++i)
		ids[PlaybackPage].push_back(play[i]->id);
	std::vector<const Audio::Stream *> rec = m_backend->streams(Audio::Recording);
	for (size_t i = 0; i < rec.size(); ++i)
		ids[RecordingPage].push_back(rec[i]->id);
	std::vector<const Audio::Device *> out = m_backend->devices(Audio::Sink);
	for (size_t i = 0; i < out.size(); ++i)
		ids[OutputPage].push_back(out[i]->id);
	std::vector<const Audio::Device *> in = m_backend->devices(Audio::Source);
	for (size_t i = 0; i < in.size(); ++i)
		ids[InputPage].push_back(in[i]->id);

	syncStrips(m_lists[PlaybackPage], m_strips[PlaybackPage], ids[PlaybackPage], ChannelStrip::StreamStrip);
	syncStrips(m_lists[RecordingPage], m_strips[RecordingPage], ids[RecordingPage], ChannelStrip::StreamStrip);
	syncStrips(m_lists[OutputPage], m_strips[OutputPage], ids[OutputPage], ChannelStrip::DeviceStrip);
	syncStrips(m_lists[InputPage], m_strips[InputPage], ids[InputPage], ChannelStrip::DeviceStrip);

	/* cards */
	const std::map<uint32_t, Audio::Card> &cards = m_backend->model().cards;
	for (std::map<uint32_t, CardRow *>::iterator it = m_cards.begin(); it != m_cards.end();) {
		if (!cards.count(it->first)) {
			delete it->second;
			m_cards.erase(it++);
		} else {
			++it;
		}
	}
	int index = 0;
	for (std::map<uint32_t, Audio::Card>::const_iterator it = cards.begin(); it != cards.end(); ++it, ++index) {
		std::map<uint32_t, CardRow *>::iterator row = m_cards.find(it->first);
		if (row != m_cards.end()) {
			row->second->updateFromModel();
		} else {
			CardRow *r = new CardRow(m_backend, it->first, m_cardList->container());
			m_cards[it->first] = r;
			m_cardList->insertRow(r, index);
		}
	}
	m_cardList->updateEmpty(cards.empty());

	if (m_backend->isConnected())
		m_status->setText(i18n("Connected to %1 (%2 backend)")
		                  .arg(q(m_backend->model().serverName).stripWhiteSpace())
		                  .arg(m_backend->backendName()));
	else
		m_status->setText(i18n("Not connected to a sound server (%1). Retrying...")
		                  .arg(m_backend->backendName()));
}

StripList *MixerWidget::currentStripList() const
{
	for (int p = 0; p < 4; ++p)
		if (m_tabs->currentPage() == m_lists[p])
			return m_lists[p];
	return 0;
}

void MixerWidget::updatePeaks()
{
	/* only the visible page */
	for (int p = 0; p < 4; ++p) {
		if (m_tabs->currentPage() != m_lists[p])
			continue;
		for (StripMap::iterator it = m_strips[p].begin(); it != m_strips[p].end(); ++it)
			it->second->updatePeak();
	}
}

void MixerWidget::slotCurrentChanged(TQWidget *)
{
	updatePeaks();
}

#include "mixerwidget.moc"
