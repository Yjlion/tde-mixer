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

#include "channelstrip.h"
#include "backend/audiobackend.h"

#include <tqcombobox.h>
#include <tqlabel.h>
#include <tqlayout.h>
#include <tqpainter.h>
#include <tqradiobutton.h>
#include <tqslider.h>
#include <tqstylesheet.h>
#include <tqtoolbutton.h>
#include <tqtooltip.h>

#include <tdeglobal.h>
#include <kiconloader.h>
#include <tdelocale.h>
#include <ksqueezedtextlabel.h>

using Audio::q;

/* ------------------------------------------------------------------ */

PeakMeter::PeakMeter(TQWidget *parent, const char *name)
	: TQWidget(parent, name, WRepaintNoErase), m_value(0.0f)
{
	setSizePolicy(TQSizePolicy(TQSizePolicy::Expanding, TQSizePolicy::Fixed));
}

TQSize PeakMeter::sizeHint() const
{
	return TQSize(100, 5);
}

void PeakMeter::setValue(float v)
{
	if (v < 0.0f)
		v = 0.0f;
	if (v > 1.0f)
		v = 1.0f;
	/* fall back slowly, rise at once */
	float shown = v >= m_value ? v : TQMAX(v, m_value - 0.06f);
	if ((int)(shown * 200) == (int)(m_value * 200))
		return;
	m_value = shown;
	update();
}

void PeakMeter::reset()
{
	m_value = 0.0f;
	update();
}

void PeakMeter::paintEvent(TQPaintEvent *)
{
	TQPainter p(this);
	TQColor bg = colorGroup().mid();
	p.fillRect(rect(), colorGroup().background().dark(108));
	int w = (int)(m_value * width() + 0.5f);
	/* green up to 75 %, then yellow, red above 95 % */
	int g = TQMIN(w, (int)(width() * 0.75));
	int y = TQMIN(w, (int)(width() * 0.95));
	if (g > 0)
		p.fillRect(0, 0, g, height(), TQColor(80, 180, 70));
	if (y > g)
		p.fillRect(g, 0, y - g, height(), TQColor(230, 190, 40));
	if (w > y)
		p.fillRect(y, 0, w - y, height(), TQColor(220, 60, 50));
	p.setPen(bg);
	p.drawRect(rect());
}

/* ------------------------------------------------------------------ */

static TQPixmap loadIconOrNull(const TQString &name, int size)
{
	if (name.isEmpty())
		return TQPixmap();
	return TDEGlobal::iconLoader()->loadIcon(name, TDEIcon::NoGroup, size,
	                                         TDEIcon::DefaultState, 0, true);
}

TQPixmap ChannelStrip::deviceIcon(const Audio::Device &d, int size)
{
	TQString n = q(d.iconName);
	TQString lower = (n + " " + q(d.name) + " " + q(d.activePort)).lower();
	TQString fallback;
	if (lower.contains("headphone") || lower.contains("headset"))
		fallback = "tde-mixer-headphones";
	else if (d.kind == Audio::Source)
		fallback = "tde-mixer-microphone";
	else
		fallback = "tde-mixer-speaker";
	TQPixmap pix = loadIconOrNull(fallback, size);
	return pix.isNull() ? TDEGlobal::iconLoader()->loadIcon("tde-mixer", TDEIcon::NoGroup, size) : pix;
}

TQPixmap ChannelStrip::streamIcon(const Audio::Stream &s, int size)
{
	TQStringList candidates;
	candidates << q(s.iconName) << q(s.binary) << q(s.binary).lower()
	           << q(s.appName).lower() << q(s.appName).lower().replace(' ', "-");
	for (TQStringList::const_iterator it = candidates.begin(); it != candidates.end(); ++it) {
		TQPixmap pix = loadIconOrNull(*it, size);
		if (!pix.isNull())
			return pix;
	}
	TQPixmap pix = loadIconOrNull(s.kind == Audio::Recording ? "tde-mixer-microphone" : "tde-mixer-application", size);
	return pix.isNull() ? TDEGlobal::iconLoader()->loadIcon("tde-mixer", TDEIcon::NoGroup, size) : pix;
}

TQString ChannelStrip::volumeIconName(float level, bool muted)
{
	if (muted || level <= 0.001f)
		return "tde-mixer-muted";
	if (level < 0.34f)
		return "tde-mixer-low";
	if (level < 0.67f)
		return "tde-mixer-medium";
	return "tde-mixer-high";
}

/* ------------------------------------------------------------------ */

ChannelStrip::ChannelStrip(AudioBackend *backend, uint32_t id, Type type, bool compact,
                           TQWidget *parent, const char *name)
	: TQFrame(parent, name), m_backend(backend), m_id(id), m_type(type), m_compact(compact),
	  m_maxVolume(100), m_dragging(false),
	  m_subtitle(0), m_default(0), m_devices(0), m_ports(0), m_portLabel(0), m_peak(0)
{
	int iconSize = compact ? 22 : 48;

	m_icon = new TQLabel(this);
	m_icon->setFixedSize(iconSize + 4, iconSize + 4);
	m_icon->setAlignment(TQt::AlignCenter);

	m_title = new KSqueezedTextLabel(this);
	m_slider = new TQSlider(0, 100, 5, 0, TQt::Horizontal, this);
	m_slider->setTracking(true);
	m_slider->setLineStep(1);
	m_slider->setMinimumWidth(compact ? 140 : 200);
	m_percent = new TQLabel(this);
	m_percent->setAlignment(TQt::AlignRight | TQt::AlignVCenter);
	m_percent->setMinimumWidth(m_percent->fontMetrics().width("150 %") + 4);
	m_mute = new TQToolButton(this);
	m_mute->setToggleButton(true);
	m_mute->setAutoRaise(true);
	TQToolTip::add(m_mute, i18n("Mute"));

	connect(m_slider, TQ_SIGNAL(valueChanged(int)), TQ_SLOT(slotSliderMoved(int)));
	connect(m_slider, TQ_SIGNAL(sliderPressed()), TQ_SLOT(slotSliderPressed()));
	connect(m_slider, TQ_SIGNAL(sliderReleased()), TQ_SLOT(slotSliderReleased()));
	connect(m_mute, TQ_SIGNAL(clicked()), TQ_SLOT(slotMute()));

	if (compact) {
		TQHBoxLayout *l = new TQHBoxLayout(this, 2, 6);
		l->addWidget(m_icon);
		TQVBoxLayout *v = new TQVBoxLayout(l, 0);
		v->addWidget(m_title);
		TQHBoxLayout *h = new TQHBoxLayout(v, 2);
		h->addWidget(m_mute);
		h->addWidget(m_slider, 1);
		h->addWidget(m_percent);
		m_title->setMinimumWidth(180);
	} else {
		setFrameStyle(TQFrame::StyledPanel | TQFrame::Raised);
		TQFont f = m_title->font();
		f.setBold(true);
		m_title->setFont(f);
		m_subtitle = new KSqueezedTextLabel(this);
		m_peak = new PeakMeter(this);

		TQHBoxLayout *l = new TQHBoxLayout(this, 6, 8);
		l->addWidget(m_icon, 0, TQt::AlignTop);
		TQVBoxLayout *v = new TQVBoxLayout(l, 2);

		TQHBoxLayout *top = new TQHBoxLayout(v, 6);
		TQVBoxLayout *names = new TQVBoxLayout(top, 0);
		names->addWidget(m_title);
		names->addWidget(m_subtitle);
		top->addStretch(0);

		if (type == DeviceStrip) {
			m_portLabel = new TQLabel(i18n("Port:"), this);
			m_ports = new TQComboBox(false, this);
			m_portLabel->setBuddy(m_ports);
			top->addWidget(m_portLabel);
			top->addWidget(m_ports);
			m_default = new TQRadioButton(i18n("Default"), this);
			TQToolTip::add(m_default, i18n("Use this device for new sounds"));
			top->addWidget(m_default);
			connect(m_default, TQ_SIGNAL(clicked()), TQ_SLOT(slotDefault()));
			connect(m_ports, TQ_SIGNAL(activated(int)), TQ_SLOT(slotPortSelected(int)));
		} else {
			m_devices = new TQComboBox(false, this);
			m_devices->setMaximumWidth(260);
			top->addWidget(m_devices);
			TQToolTip::add(m_devices, i18n("The device this application uses"));
			connect(m_devices, TQ_SIGNAL(activated(int)), TQ_SLOT(slotDeviceSelected(int)));
		}

		TQHBoxLayout *h = new TQHBoxLayout(v, 4);
		h->addWidget(m_mute);
		h->addWidget(m_slider, 1);
		h->addWidget(m_percent);

		TQHBoxLayout *ph = new TQHBoxLayout(v, 4);
		ph->addSpacing(m_mute->sizeHint().width() + 4);
		ph->addWidget(m_peak, 1);
		ph->addSpacing(m_percent->minimumWidth() + 4);

		m_slider->setTickmarks(TQSlider::Below);
		m_slider->setTickInterval(25);
	}

	updateFromModel();
}

ChannelStrip::~ChannelStrip()
{
}

void ChannelStrip::setMaxVolume(int percent)
{
	if (percent == m_maxVolume)
		return;
	m_maxVolume = percent;
	updateFromModel();
}

void ChannelStrip::setPeaksVisible(bool visible)
{
	if (!m_peak)
		return;
	if (visible)
		m_peak->show();
	else
		m_peak->hide();
}

void ChannelStrip::updatePeak()
{
	if (!m_peak || !m_peak->isVisible())
		return;
	float v = m_backend->peak(m_id);
	if (v < 0.0f)
		m_peak->setValue(0.0f);
	else
		m_peak->setValue(v);
}

void ChannelStrip::updateFromModel()
{
	const Audio::Device *dev = m_type == DeviceStrip ? m_backend->device(m_id) : 0;
	const Audio::Stream *st = m_type == StreamStrip ? m_backend->stream(m_id) : 0;
	if (!dev && !st)
		return;

	const std::vector<float> &volumes = dev ? dev->volumes : st->volumes;
	bool muted = dev ? dev->muted : st->muted;
	bool hasVolume = dev ? dev->hasVolume : st->hasVolume;
	float lvl = Audio::level(volumes);

	/* icon and names */
	TQString iconKey = dev ? q(dev->iconName + "|" + dev->activePort) : q(st->iconName + "|" + st->binary + "|" + st->appName);
	if (iconKey != m_iconKey || !m_icon->pixmap()) {
		m_iconKey = iconKey;
		int size = m_compact ? 22 : 48;
		m_icon->setPixmap(dev ? deviceIcon(*dev, size) : streamIcon(*st, size));
	}

	TQString title, subtitle, tip;
	if (dev) {
		title = q(dev->description);
		subtitle = q(dev->name);
	} else {
		title = q(st->appName);
		subtitle = q(st->mediaName);
		if (st->corked)
			subtitle = i18n("%1 (paused)").arg(subtitle);
	}
	m_title->setText(title);
	if (m_subtitle)
		m_subtitle->setText(subtitle);

	/* channel volumes in the tooltip */
	if (volumes.size() > 1) {
		const std::vector<std::string> &ch = dev ? dev->channels : st->channels;
		TQStringList parts;
		for (size_t i = 0; i < volumes.size(); ++i)
			parts << i18n("channel: volume", "%1: %2 %").arg(i < ch.size() ? q(ch[i]) : TQString::number(i + 1))
			                                              .arg((int)(volumes[i] * 100 + 0.5f));
		tip = parts.join(", ");
	}
	TQToolTip::remove(m_slider);
	if (!tip.isEmpty())
		TQToolTip::add(m_slider, tip);

	/* slider: not while the user moves it */
	int percent = (int)(lvl * 100 + 0.5f);
	int max = TQMAX(m_maxVolume, percent);
	bool recent = m_lastUserChange.isValid() && m_lastUserChange.elapsed() < 500;
	if (!m_dragging && !recent) {
		m_slider->blockSignals(true);
		if (m_slider->maxValue() != max)
			m_slider->setMaxValue(max);
		m_slider->setValue(percent);
		m_slider->blockSignals(false);
		m_percent->setText(i18n("%1 %").arg(percent));
	}
	m_slider->setEnabled(hasVolume);

	m_mute->setOn(muted);
	m_mute->setIconSet(SmallIconSet(volumeIconName(lvl, muted), m_compact ? 16 : 22));
	TQToolTip::remove(m_mute);
	TQToolTip::add(m_mute, muted ? i18n("Unmute") : i18n("Mute"));

	if (dev && m_default) {
		const Audio::Device *def = m_backend->defaultDevice(dev->kind);
		m_default->setChecked(def && def->id == dev->id);
	}

	if (dev && m_ports) {
		TQStringList texts;
		std::vector<std::string> names;
		int current = -1;
		for (size_t i = 0; i < dev->ports.size(); ++i) {
			TQString text = q(dev->ports[i].description);
			if (!dev->ports[i].available)
				text = i18n("%1 (unplugged)").arg(text);
			texts << text;
			names.push_back(dev->ports[i].name);
			if (dev->ports[i].name == dev->activePort)
				current = (int)i;
		}
		/* only refill when something changed, an open list would close */
		if (names != m_portNames || texts != m_portTexts) {
			m_ports->clear();
			m_ports->insertStringList(texts);
			m_portNames = names;
			m_portTexts = texts;
		}
		if (current >= 0 && m_ports->currentItem() != current)
			m_ports->setCurrentItem(current);
		bool show = dev->ports.size() > 1;
		m_ports->setShown(show);
		m_portLabel->setShown(show);
	}

	if (st && m_devices) {
		Audio::DeviceKind kind = st->kind == Audio::Playback ? Audio::Sink : Audio::Source;
		std::vector<const Audio::Device *> devs = m_backend->devices(kind, kind == Audio::Source);
		TQStringList texts;
		std::vector<uint32_t> ids;
		int current = -1;
		for (size_t i = 0; i < devs.size(); ++i) {
			TQString text = q(devs[i]->description);
			if (devs[i]->isMonitor)
				text = i18n("Monitor of %1").arg(text);
			texts << text;
			ids.push_back(devs[i]->id);
			if (devs[i]->id == st->deviceId)
				current = (int)i;
		}
		if (current < 0) {
			texts << i18n("(not connected)");
			ids.push_back(Audio::InvalidId);
			current = texts.count() - 1;
		}
		if (ids != m_deviceIds || texts != m_deviceTexts) {
			m_devices->clear();
			m_devices->insertStringList(texts);
			m_deviceIds = ids;
			m_deviceTexts = texts;
		}
		if (m_devices->currentItem() != current)
			m_devices->setCurrentItem(current);
		m_devices->setEnabled(devs.size() > 0);
	}
}

void ChannelStrip::setLevel(int percent)
{
	m_lastUserChange.start();
	m_percent->setText(i18n("%1 %").arg(percent));
	float lvl = percent / 100.0f;
	if (m_type == DeviceStrip)
		m_backend->setDeviceLevel(m_id, lvl);
	else
		m_backend->setStreamLevel(m_id, lvl);
}

void ChannelStrip::slotSliderMoved(int value)
{
	setLevel(value);
}

void ChannelStrip::slotSliderPressed()
{
	m_dragging = true;
}

void ChannelStrip::slotSliderReleased()
{
	m_dragging = false;
	setLevel(m_slider->value());
}

void ChannelStrip::slotMute()
{
	bool mute = m_mute->isOn();
	if (m_type == DeviceStrip)
		m_backend->setDeviceMute(m_id, mute);
	else
		m_backend->setStreamMute(m_id, mute);
}

void ChannelStrip::slotDefault()
{
	m_backend->setDefaultDevice(m_id);
}

void ChannelStrip::slotDeviceSelected(int index)
{
	if (index < 0 || index >= (int)m_deviceIds.size() || m_deviceIds[index] == Audio::InvalidId)
		return;
	m_backend->moveStream(m_id, m_deviceIds[index]);
}

void ChannelStrip::slotPortSelected(int index)
{
	if (index < 0 || index >= (int)m_portNames.size())
		return;
	m_backend->setDevicePort(m_id, m_portNames[index]);
}

#include "channelstrip.moc"
