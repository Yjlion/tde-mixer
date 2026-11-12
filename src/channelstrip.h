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

#ifndef CHANNELSTRIP_H
#define CHANNELSTRIP_H

#include <stdint.h>

#include <string>
#include <vector>

#include <tqframe.h>
#include <tqdatetime.h>
#include <tqpixmap.h>
#include <tqstringlist.h>

class AudioBackend;
class TQComboBox;
class TQLabel;
class TQRadioButton;
class TQSlider;
class TQToolButton;
class KSqueezedTextLabel;

namespace Audio { struct Device; struct Stream; }

/** a thin horizontal level meter */
class PeakMeter : public TQWidget
{
	TQ_OBJECT
public:
	PeakMeter(TQWidget *parent = 0, const char *name = 0);
	/** 0..1; values below the current one decay slowly */
	void setValue(float v);
	void reset();
	virtual TQSize sizeHint() const;
protected:
	virtual void paintEvent(TQPaintEvent *);
private:
	float m_value;
};

/**
 * One row of the mixer: a device (output or input) or a stream (an
 * application playing or recording). Shows the icon, name, a volume
 * slider, mute and, depending on the type, the default device, the port or
 * the device a stream uses.
 *
 * Compact strips (in the tray popup) only have the icon, name, slider and
 * mute button.
 */
class ChannelStrip : public TQFrame
{
	TQ_OBJECT
public:
	enum Type { DeviceStrip, StreamStrip };

	ChannelStrip(AudioBackend *backend, uint32_t id, Type type, bool compact,
	             TQWidget *parent = 0, const char *name = 0);
	virtual ~ChannelStrip();

	uint32_t objectId() const { return m_id; }
	Type stripType() const { return m_type; }

	/** reads the model again */
	void updateFromModel();
	void setMaxVolume(int percent);
	void setPeaksVisible(bool visible);
	void updatePeak();

	/* icons, also used by the tray */
	static TQPixmap deviceIcon(const Audio::Device &d, int size);
	static TQPixmap streamIcon(const Audio::Stream &s, int size);
	static TQString volumeIconName(float level, bool muted);

private slots:
	void slotSliderMoved(int value);
	void slotSliderPressed();
	void slotSliderReleased();
	void slotMute();
	void slotDefault();
	void slotDeviceSelected(int index);
	void slotPortSelected(int index);

private:
	void setLevel(int percent);

	AudioBackend *m_backend;
	uint32_t m_id;
	Type m_type;
	bool m_compact;
	int m_maxVolume;
	bool m_dragging;
	TQTime m_lastUserChange;
	TQString m_iconKey;

	TQLabel *m_icon;
	KSqueezedTextLabel *m_title;
	KSqueezedTextLabel *m_subtitle;
	TQSlider *m_slider;
	TQLabel *m_percent;
	TQToolButton *m_mute;
	TQRadioButton *m_default;
	TQComboBox *m_devices;
	TQComboBox *m_ports;
	TQLabel *m_portLabel;
	PeakMeter *m_peak;
	std::vector<uint32_t> m_deviceIds;
	std::vector<std::string> m_portNames;
	TQStringList m_deviceTexts;
	TQStringList m_portTexts;
};

#endif
