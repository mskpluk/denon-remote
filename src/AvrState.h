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

// One input as an older receiver lists it on its web page (WebCatalogue).
struct WebSource {
    QString param;    // protocol name, as SI takes it
    QString label;    // name configured on the receiver; empty if none
    bool enabled = true;
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
    // Number of Quick Select slots: four unless the receiver reports a fifth.
    int quickSelectSlots() const;
    // Name the owner gave a slot on the receiver; empty if none was reported.
    QString quickSelectName(int slot) const { return m_quickSelectNames.value(slot); }

    // Zones 2 and 3. A zone counts as present once the receiver has said
    // anything about it; a model without one ignores the query.
    bool zonePresent(int zone) const { return extra(zone).present; }
    bool zonePower(int zone) const { return extra(zone).power; }
    double zoneVolume(int zone) const { return extra(zone).volume; }
    bool zoneMute(int zone) const { return extra(zone).mute; }
    QString zoneSource(int zone) const { return extra(zone).source; }

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
    // The input list for zones 2 and 3, which can also follow the main zone.
    QVector<SourceEntry> zoneSources() const;
    QString sourceLabel(const QString &param) const;

    // Whether the receiver has answered SSFUN or SSSOD. The source list is
    // then read from telnet alone.
    bool catalogueReported() const { return m_catalogueReported; }
    // Stands in for SSFUN/SSSOD on receivers that do not answer them; ignored
    // as soon as they do.
    void setWebSources(const QVector<WebSource> &sources);

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
    void quickSelectNamesChanged();

    void zonePresenceChanged(int zone);
    void zonePowerChanged(int zone);
    void zoneVolumeChanged(int zone);
    void zoneMuteChanged(int zone);
    void zoneSourceChanged(int zone);

    void toneChanged();
    void drcChanged(const QString &value);
    void restorerChanged(const QString &value);
    void ecoChanged(const QString &value);
    void sleepChanged(int minutes);

    void sourceCatalogChanged();

private:
    struct ExtraZone {
        bool present = false;
        bool power = false;
        double volume = 0.0;
        bool mute = false;
        QString source;
    };
    const ExtraZone &extra(int zone) const { return m_zones[zone == 3 ? 1 : 0]; }
    ExtraZone &extra(int zone) { return m_zones[zone == 3 ? 1 : 0]; }

    bool handleZone(int zone, const QString &line);
    bool handleParameter(const QString &line);
    bool handleSetup(const QString &line);
    void noteSource(const QString &param);
    void noteQuickSelectSlot(int slot);
    bool usingWebSources() const { return m_haveWebSources && !m_catalogueReported; }
    QMap<QString, QString> effectiveLabels() const;

    bool m_power = false;
    double m_volume = 0.0;
    double m_volumeMax = 98.0;
    bool m_mute = false;
    QString m_source;
    QString m_soundMode;
    int m_quickSelect = 0;

    ExtraZone m_zones[2];  // zone 2, zone 3

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
    // availableSources alone cannot tell whether SSFUN/SSSOD answered,
    // because the current input lands there too.
    bool m_catalogueReported = false;
    QVector<WebSource> m_webSources;
    bool m_haveWebSources = false;

    QMap<int, QString> m_quickSelectNames;  // slot -> name reported by SSQSNZMA
    int m_highestQuickSlot = 0;             // highest slot the AVR has mentioned
};
