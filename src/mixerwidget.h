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

#ifndef MIXERWIDGET_H
#define MIXERWIDGET_H

#include <stdint.h>

#include <map>
#include <vector>

#include <tqscrollview.h>
#include <tqwidget.h>

#include "channelstrip.h"

class AudioBackend;
class TQComboBox;
class TQLabel;
class TQTabWidget;
class TQTimer;
class TQVBoxLayout;
struct MixerSettings;

/** a scrollable list of rows with a message when it is empty */
class StripList : public TQScrollView
{
	TQ_OBJECT
public:
	StripList(const TQString &emptyText, TQWidget *parent = 0, const char *name = 0);

	TQWidget *container() const { return m_box; }
	/** inserts a row at @p index (rows are kept in insertion order) */
	void insertRow(TQWidget *w, int index);
	void updateEmpty(bool empty);

protected:
	virtual void viewportResizeEvent(TQResizeEvent *e);

private:
	TQWidget *m_box;
	TQVBoxLayout *m_layout;
	TQLabel *m_empty;
};

/** a sound card and its profile */
class CardRow : public TQFrame
{
	TQ_OBJECT
public:
	CardRow(AudioBackend *backend, uint32_t id, TQWidget *parent = 0);
	uint32_t objectId() const { return m_id; }
	void updateFromModel();
private slots:
	void slotProfileSelected(int index);
private:
	AudioBackend *m_backend;
	uint32_t m_id;
	TQLabel *m_icon;
	TQLabel *m_title;
	TQLabel *m_subtitle;
	TQComboBox *m_profiles;
	std::vector<std::string> m_names;
	TQStringList m_texts;
};

/**
 * The mixer: tabs for applications playing sound, applications recording,
 * output devices, input devices and sound card profiles. Used by the
 * standalone application and by the Control Center module.
 */
class MixerWidget : public TQWidget
{
	TQ_OBJECT
public:
	enum Page { PlaybackPage = 0, RecordingPage, OutputPage, InputPage, CardsPage };

	/** uses @p backend, or creates one from the settings if it is 0 */
	MixerWidget(AudioBackend *backend, TQWidget *parent = 0, const char *name = 0);
	virtual ~MixerWidget();

	AudioBackend *backend() const { return m_backend; }
	void applySettings(const MixerSettings &settings);

	/** playback, recording, output, input or configuration */
	void showPage(const TQString &page);
	void showPage(Page page);
	/** adds a further tab (the Control Center module adds its settings) */
	void addPage(TQWidget *page, const TQString &label);

private slots:
	void sync();
	void updatePeaks();
	void slotCurrentChanged(TQWidget *page);

private:
	typedef std::map<uint32_t, ChannelStrip *> StripMap;
	void syncStrips(StripList *list, StripMap &strips, const std::vector<uint32_t> &ids,
	                ChannelStrip::Type type);
	StripList *currentStripList() const;

	AudioBackend *m_backend;
	bool m_ownBackend;
	TQTabWidget *m_tabs;
	StripList *m_lists[4];
	StripMap m_strips[4];
	StripList *m_cardList;
	std::map<uint32_t, CardRow *> m_cards;
	TQLabel *m_status;
	TQTimer *m_peakTimer;
	int m_maxVolume;
	bool m_showPeaks;
};

#endif
