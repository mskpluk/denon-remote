#pragma once

#include <QString>
#include <QWidget>

class AvrState;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;

// Power, volume, mute and input for one zone.
//
// All zones expose the same controls and differ only in the command prefix,
// so one widget covers the main zone, zone 2 and zone 3 alike.
class ZonePanel : public QWidget
{
    Q_OBJECT

public:
    enum class Zone { Main, Zone2, Zone3 };

    ZonePanel(Zone zone, AvrState *state, QWidget *parent = nullptr);

signals:
    void commandRequested(const QString &command);

private slots:
    void onPowerToggled(bool on);
    void onMuteToggled(bool muted);
    void onSourceActivated(int index);
    void onSliderChanged(int halfSteps);
    void sendPendingVolume();

    void refreshPower();
    void refreshVolume();
    void refreshMute();
    void refreshSource();
    void refreshCatalogue();

private:
    bool isMain() const { return m_zone == Zone::Main; }
    int zoneNumber() const { return m_zone == Zone::Zone3 ? 3 : 2; }
    // "Z2" or "Z3"; the main zone has a different prefix per command.
    QString zonePrefix() const { return QStringLiteral("Z%1").arg(zoneNumber()); }
    void onZoneSignal(int zone, void (ZonePanel::*refresh)());
    double currentVolume() const;

    Zone m_zone;
    AvrState *m_state;

    QPushButton *m_power;
    QPushButton *m_mute;
    QComboBox *m_source;
    QSlider *m_volume;
    QLabel *m_volumeLabel;
    QTimer *m_volumeSendTimer;

    // Set while widgets are being written from the mirrored state, so that
    // reacting to the AVR never bounces a command straight back at it.
    bool m_updating = false;
};
