#include "ZonePanel.h"

#include "AvrState.h"

#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>

ZonePanel::ZonePanel(Zone zone, AvrState *state, QWidget *parent)
    : QWidget(parent)
    , m_zone(zone)
    , m_state(state)
    , m_power(new QPushButton(tr("Power"), this))
    , m_mute(new QPushButton(tr("Mute"), this))
    , m_source(new QComboBox(this))
    , m_volume(new QSlider(Qt::Horizontal, this))
    , m_volumeLabel(new QLabel(this))
    , m_volumeSendTimer(new QTimer(this))
{
    m_power->setCheckable(true);
    m_mute->setCheckable(true);
    m_volume->setRange(0, int(m_state->volumeMax() * 2));
    m_volume->setSingleStep(1);   // one half-decibel step
    m_volume->setPageStep(10);
    m_volume->setTracking(true);

    QFont readout = m_volumeLabel->font();
    readout.setPointSizeF(readout.pointSizeF() * 2.0);
    readout.setBold(true);
    m_volumeLabel->setFont(readout);
    m_volumeLabel->setAlignment(Qt::AlignCenter);
    m_volumeLabel->setMinimumWidth(140);
    m_volumeLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto *down = new QPushButton(QStringLiteral("−"), this);
    auto *up = new QPushButton(QStringLiteral("+"), this);
    down->setToolTip(tr("Volume down"));
    up->setToolTip(tr("Volume up"));
    for (QPushButton *button : {down, up}) {
        button->setAutoRepeat(true);
        button->setAutoRepeatDelay(400);
        button->setAutoRepeatInterval(180);
        button->setFixedWidth(44);
    }

    auto *layout = new QGridLayout(this);
    layout->addWidget(new QLabel(tr("Input"), this), 0, 0);
    layout->addWidget(m_source, 0, 1, 1, 3);
    layout->addWidget(m_volumeLabel, 1, 0, 1, 4);
    layout->addWidget(down, 2, 0);
    layout->addWidget(m_volume, 2, 1, 1, 2);
    layout->addWidget(up, 2, 3);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_power);
    buttons->addWidget(m_mute);
    layout->addLayout(buttons, 3, 0, 1, 4);
    layout->setColumnStretch(1, 1);
    layout->setColumnStretch(2, 1);
    // Spare height collects at the bottom instead of stretching the readout.
    layout->setRowStretch(4, 1);

    // Dragging the slider would otherwise flood the receiver, so the value is
    // held briefly and only the latest one is sent.
    m_volumeSendTimer->setSingleShot(true);
    m_volumeSendTimer->setInterval(120);

    const QString volumePrefix = isMain() ? QStringLiteral("MV") : zonePrefix();
    connect(down, &QPushButton::clicked, this, [this, volumePrefix] {
        emit commandRequested(volumePrefix + QStringLiteral("DOWN"));
    });
    connect(up, &QPushButton::clicked, this, [this, volumePrefix] {
        emit commandRequested(volumePrefix + QStringLiteral("UP"));
    });

    connect(m_power, &QPushButton::toggled, this, &ZonePanel::onPowerToggled);
    connect(m_mute, &QPushButton::toggled, this, &ZonePanel::onMuteToggled);
    connect(m_source, &QComboBox::activated, this, &ZonePanel::onSourceActivated);
    connect(m_volume, &QSlider::valueChanged, this, &ZonePanel::onSliderChanged);
    connect(m_volumeSendTimer, &QTimer::timeout, this, &ZonePanel::sendPendingVolume);

    if (isMain()) {
        connect(m_state, &AvrState::powerChanged, this, &ZonePanel::refreshPower);
        connect(m_state, &AvrState::volumeChanged, this, &ZonePanel::refreshVolume);
        connect(m_state, &AvrState::muteChanged, this, &ZonePanel::refreshMute);
        connect(m_state, &AvrState::sourceChanged, this, &ZonePanel::refreshSource);
    } else {
        connect(m_state, &AvrState::zonePowerChanged, this,
                [this](int zone) { onZoneSignal(zone, &ZonePanel::refreshPower); });
        connect(m_state, &AvrState::zoneVolumeChanged, this,
                [this](int zone) { onZoneSignal(zone, &ZonePanel::refreshVolume); });
        connect(m_state, &AvrState::zoneMuteChanged, this,
                [this](int zone) { onZoneSignal(zone, &ZonePanel::refreshMute); });
        connect(m_state, &AvrState::zoneSourceChanged, this,
                [this](int zone) { onZoneSignal(zone, &ZonePanel::refreshSource); });
    }
    connect(m_state, &AvrState::sourceCatalogChanged, this, &ZonePanel::refreshCatalogue);

    refreshCatalogue();
    refreshPower();
    refreshVolume();
    refreshMute();
}

void ZonePanel::onZoneSignal(int zone, void (ZonePanel::*refresh)())
{
    if (zone == zoneNumber())
        (this->*refresh)();
}

double ZonePanel::currentVolume() const
{
    return isMain() ? m_state->volume() : m_state->zoneVolume(zoneNumber());
}

void ZonePanel::onPowerToggled(bool on)
{
    if (m_updating)
        return;
    if (isMain())
        emit commandRequested(on ? QStringLiteral("PWON") : QStringLiteral("PWSTANDBY"));
    else
        emit commandRequested(zonePrefix() + (on ? QStringLiteral("ON") : QStringLiteral("OFF")));
}

void ZonePanel::onMuteToggled(bool muted)
{
    if (m_updating)
        return;
    const QString prefix = isMain() ? QStringLiteral("MU") : zonePrefix() + QStringLiteral("MU");
    emit commandRequested(prefix + (muted ? QStringLiteral("ON") : QStringLiteral("OFF")));
}

void ZonePanel::onSourceActivated(int index)
{
    if (m_updating || index < 0)
        return;
    const QString param = m_source->itemData(index).toString();
    if (param.isEmpty())
        return;
    emit commandRequested((isMain() ? QStringLiteral("SI") : zonePrefix()) + param);
}

void ZonePanel::onSliderChanged(int halfSteps)
{
    m_volumeLabel->setText(AvrState::formatDecibel(halfSteps / 2.0));
    if (m_updating)
        return;
    m_volumeSendTimer->start();
}

void ZonePanel::sendPendingVolume()
{
    const QString argument = AvrState::volumeArgument(m_volume->value() / 2.0);
    emit commandRequested((isMain() ? QStringLiteral("MV") : zonePrefix()) + argument);
}

void ZonePanel::refreshPower()
{
    m_updating = true;
    m_power->setChecked(isMain() ? m_state->power() : m_state->zonePower(zoneNumber()));
    m_updating = false;
}

void ZonePanel::refreshVolume()
{
    // A pending drag would be undone by an echo of an older value.
    if (m_volumeSendTimer->isActive() || m_volume->isSliderDown())
        return;

    m_updating = true;
    m_volume->setRange(0, int(m_state->volumeMax() * 2));
    m_volume->setValue(qRound(currentVolume() * 2.0));
    m_volumeLabel->setText(AvrState::formatDecibel(currentVolume()));
    m_updating = false;
}

void ZonePanel::refreshMute()
{
    m_updating = true;
    m_mute->setChecked(isMain() ? m_state->mute() : m_state->zoneMute(zoneNumber()));
    m_updating = false;
}

void ZonePanel::refreshSource()
{
    const QString param = isMain() ? m_state->source() : m_state->zoneSource(zoneNumber());
    m_updating = true;
    const int index = m_source->findData(param);
    if (index >= 0)
        m_source->setCurrentIndex(index);
    m_updating = false;
}

void ZonePanel::refreshCatalogue()
{
    const QVector<SourceEntry> entries = isMain() ? m_state->sources() : m_state->zoneSources();

    m_updating = true;
    m_source->clear();
    for (const SourceEntry &entry : entries)
        m_source->addItem(entry.label, entry.param);
    m_updating = false;

    refreshSource();
}
