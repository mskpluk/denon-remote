#pragma once

#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

// One selectable input, as the receiver describes it.
struct SourceEntry {
    QString param;  // protocol name, e.g. "SAT/CBL"
    QString label;  // name shown on the AVR, e.g. "CBL/SAT"
};

// Mirrors the receiver's state by parsing the event stream from AvrConnection.
//
// Every value here is what the AVR last reported, never what the UI asked for,
// so the window stays in step with the front panel and the physical remote.
class AvrState : public QObject
{
    Q_OBJECT

public:
    explicit AvrState(QObject *parent = nullptr);

    // Master zone
    bool power() const { return m_power; }
    double volume() const { return m_volume; }        // raw scale, 0 .. volumeMax
    double volumeMax() const { return m_volumeMax; }
    bool mute() const { return m_mute; }
    QString source() const { return m_source; }
    QString soundMode() const { return m_soundMode; }
    int quickSelect() const { return m_quickSelect; }  // 0 when none is active

    // Zone 2
    bool zone2Power() const { return m_zone2Power; }
    double zone2Volume() const { return m_zone2Volume; }
    bool zone2Mute() const { return m_zone2Mute; }
    QString zone2Source() const { return m_zone2Source; }

    // Sound shaping
    int bass() const { return m_bass; }               // dB, 0 is flat
    int treble() const { return m_treble; }
    int subwoofer() const { return m_subwoofer; }
    bool toneControl() const { return m_toneControl; }
    // Subwoofer trim is only accepted while the receiver's own subwoofer
    // level adjustment is enabled, which is a setup-menu setting.
    bool subwooferAdjust() const { return m_subwooferAdjust; }
    QString drc() const { return m_drc; }
    QString restorer() const { return m_restorer; }
    QString eco() const { return m_eco; }
    int sleepMinutes() const { return m_sleepMinutes; }  // 0 when off

    QVector<SourceEntry> sources() const;
    QVector<SourceEntry> zone2Sources() const;
    QString sourceLabel(const QString &param) const;

    // The AVR reports volume on a 0..98 scale where 80 is 0 dB.
    static double rawToDecibel(double raw) { return raw - 80.0; }
    static QString formatDecibel(double raw);
    // Renders a raw value the way the protocol expects: "36" or "365" for 36.5.
    static QString volumeArgument(double raw);

public slots:
    void handleLine(const QString &line);
    void reset();

signals:
    void powerChanged(bool on);
    void volumeChanged(double raw);
    void muteChanged(bool muted);
    void sourceChanged(const QString &param);
    void soundModeChanged(const QString &mode);
    void quickSelectChanged(int index);

    void zone2PowerChanged(bool on);
    void zone2VolumeChanged(double raw);
    void zone2MuteChanged(bool muted);
    void zone2SourceChanged(const QString &param);

    void toneChanged();
    void drcChanged(const QString &value);
    void restorerChanged(const QString &value);
    void ecoChanged(const QString &value);
    void sleepChanged(int minutes);

    void sourceCatalogChanged();

private:
    bool handleZone2(const QString &line);
    bool handleParameter(const QString &line);
    bool handleSetup(const QString &line);
    void noteSource(const QString &param);

    bool m_power = false;
    double m_volume = 0.0;
    double m_volumeMax = 98.0;
    bool m_mute = false;
    QString m_source;
    QString m_soundMode;
    int m_quickSelect = 0;

    bool m_zone2Power = false;
    double m_zone2Volume = 0.0;
    bool m_zone2Mute = false;
    QString m_zone2Source;

    int m_bass = 0;
    int m_treble = 0;
    int m_subwoofer = 0;
    bool m_toneControl = false;
    bool m_subwooferAdjust = false;
    QString m_drc;
    QString m_restorer;
    QString m_eco;
    int m_sleepMinutes = 0;

    QMap<QString, QString> m_sourceLabels;  // param -> label reported by SSFUN
    QSet<QString> m_availableSources;       // params the AVR admits to having
    QSet<QString> m_deletedSources;         // params hidden in the AVR's setup
};
