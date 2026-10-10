#include "AvrState.h"

#include <QStringList>

namespace {

struct CanonicalSource {
    const char *param;
    const char *label;
    // Bluetooth is never listed in SSFUN or SSSOD because it can be neither
    // renamed nor hidden, so it has to be offered on its own account.
    bool alwaysAvailable;
};

// Canonical ordering for the input list. Labels are only defaults: whatever
// SSFUN reports for a given input wins, because that is the name the owner
// actually configured on the receiver.
const CanonicalSource kCanonicalSources[] = {
    {"PHONO",   "Phono",        false},
    {"CD",      "CD",           false},
    {"TUNER",   "Tuner",        false},
    {"DVD",     "DVD",          false},
    {"BD",      "Blu-ray",      false},
    {"TV",      "TV Audio",     false},
    {"SAT/CBL", "CBL/SAT",      false},
    {"MPLAY",   "Media Player", false},
    {"GAME",    "Game",         false},
    {"AUX1",    "AUX",          false},
    {"AUX2",    "AUX2",         false},
    {"NET",     "HEOS Music",   false},
    {"BT",      "Bluetooth",    true},
};

bool isAllDigits(const QString &text)
{
    if (text.isEmpty())
        return false;
    for (const QChar c : text) {
        if (!c.isDigit())
            return false;
    }
    return true;
}

// Turns "36" into 36.0 and "365" into 36.5.
bool parseVolume(const QString &digits, double *out)
{
    if (!isAllDigits(digits) || digits.size() < 2 || digits.size() > 3)
        return false;
    double value = digits.left(2).toInt();
    if (digits.size() == 3)
        value += digits.mid(2).toInt() / 10.0;
    *out = value;
    return true;
}

} // namespace

AvrState::AvrState(QObject *parent)
    : QObject(parent)
{
}

QString AvrState::formatDecibel(double raw)
{
    const double db = rawToDecibel(raw);
    return QStringLiteral("%1%2 dB")
        .arg(db > 0 ? QStringLiteral("+") : QString())
        .arg(db, 0, 'f', 1);
}

QString AvrState::volumeArgument(double raw)
{
    const int halfSteps = qRound(raw * 2.0);
    const int whole = halfSteps / 2;
    return (halfSteps % 2) ? QString::asprintf("%02d5", whole)
                           : QString::asprintf("%02d", whole);
}

void AvrState::reset()
{
    m_sourceLabels.clear();
    m_availableSources.clear();
    m_deletedSources.clear();
    m_catalogueReported = false;
    m_webSources.clear();
    m_haveWebSources = false;
    m_source.clear();
    m_soundMode.clear();
    m_quickSelectNames.clear();
    m_highestQuickSlot = 0;
    // Options a model may not have stay hidden until the new receiver
    // reports them.
    m_drc.clear();
    m_restorer.clear();
    m_eco.clear();
    for (int zone : {2, 3}) {
        extra(zone) = ExtraZone();
        emit zonePresenceChanged(zone);
    }
    emit sourceCatalogChanged();
    emit quickSelectNamesChanged();
    emit drcChanged(QString());
    emit restorerChanged(QString());
    emit ecoChanged(QString());
}

int AvrState::quickSelectSlots() const
{
    // The window has room for five, the most any model offers.
    return qBound(4, m_highestQuickSlot, 5);
}

void AvrState::setWebSources(const QVector<WebSource> &sources)
{
    m_webSources = sources;
    m_haveWebSources = true;
    if (!m_catalogueReported)
        emit sourceCatalogChanged();
}

QMap<QString, QString> AvrState::effectiveLabels() const
{
    if (!usingWebSources())
        return m_sourceLabels;
    QMap<QString, QString> labels;
    for (const WebSource &source : m_webSources) {
        if (!source.label.isEmpty())
            labels.insert(source.param, source.label);
    }
    // Whatever telnet said still wins.
    labels.insert(m_sourceLabels);
    return labels;
}

QVector<SourceEntry> AvrState::sources() const
{
    QVector<SourceEntry> result;
    QSet<QString> emitted;

    const bool web = usingWebSources();
    QSet<QString> available = m_availableSources;
    QSet<QString> deleted = m_deletedSources;
    if (web) {
        for (const WebSource &source : m_webSources) {
            available.insert(source.param);
            if (!source.enabled)
                deleted.insert(source.param);
        }
    }
    const QMap<QString, QString> labels = effectiveLabels();

    // Before the AVR has said which inputs it has there is nothing to filter
    // against, so the full canonical list stands in.
    const bool haveCatalogue = m_catalogueReported || web;

    for (const CanonicalSource &candidate : kCanonicalSources) {
        const QString param = QString::fromLatin1(candidate.param);
        if (deleted.contains(param))
            continue;
        // The web page lists Bluetooth like any other input where the model
        // has one; only SSFUN leaves it out.
        const bool listedAnyway = candidate.alwaysAvailable && !web;
        if (haveCatalogue && !listedAnyway && !available.contains(param))
            continue;
        result.append({param, labels.value(param, QString::fromLatin1(candidate.label))});
        emitted.insert(param);
    }

    // Inputs this build has never heard of still belong in the list.
    QStringList extras;
    for (const QString &param : std::as_const(available)) {
        if (!emitted.contains(param) && !deleted.contains(param))
            extras.append(param);
    }
    extras.sort();
    for (const QString &param : std::as_const(extras))
        result.append({param, labels.value(param, param)});

    return result;
}

QVector<SourceEntry> AvrState::zoneSources() const
{
    QVector<SourceEntry> result;
    result.append({QStringLiteral("SOURCE"), tr("Follow main zone")});
    result.append(sources());
    return result;
}

QString AvrState::sourceLabel(const QString &param) const
{
    if (param.isEmpty())
        return QString();
    if (param == QLatin1String("SOURCE"))
        return tr("Follow main zone");
    const QMap<QString, QString> labels = effectiveLabels();
    if (labels.contains(param))
        return labels.value(param);
    for (const CanonicalSource &candidate : kCanonicalSources) {
        if (param == QLatin1String(candidate.param))
            return QString::fromLatin1(candidate.label);
    }
    return param;
}

void AvrState::handleLine(const QString &line)
{
    if (line.startsWith(QLatin1String("SS"))) {
        if (handleSetup(line))
            return;
    }
    if (line.startsWith(QLatin1String("Z2"))) {
        if (handleZone(2, line))
            return;
    }
    if (line.startsWith(QLatin1String("Z3"))) {
        if (handleZone(3, line))
            return;
    }
    if (line.startsWith(QLatin1String("PS"))) {
        if (handleParameter(line))
            return;
    }

    if (line == QLatin1String("PWON") || line == QLatin1String("PWSTANDBY")) {
        const bool on = line.endsWith(QLatin1String("ON"));
        if (m_power != on) {
            m_power = on;
            emit powerChanged(on);
        }
        return;
    }

    // MVMAX carries the ceiling configured on the AVR and must be checked
    // before the plain volume report.
    if (line.startsWith(QLatin1String("MVMAX"))) {
        double maximum = 0.0;
        if (parseVolume(line.mid(5).trimmed(), &maximum) && maximum > 0.0) {
            m_volumeMax = maximum;
            emit volumeChanged(m_volume);
        }
        return;
    }
    if (line.startsWith(QLatin1String("MV"))) {
        double value = 0.0;
        if (parseVolume(line.mid(2), &value) && !qFuzzyCompare(m_volume, value)) {
            m_volume = value;
            emit volumeChanged(value);
        }
        return;
    }
    if (line == QLatin1String("MUON") || line == QLatin1String("MUOFF")) {
        const bool muted = line == QLatin1String("MUON");
        if (m_mute != muted) {
            m_mute = muted;
            emit muteChanged(muted);
        }
        return;
    }
    if (line.startsWith(QLatin1String("SI"))) {
        const QString param = line.mid(2).trimmed();
        noteSource(param);
        if (m_source != param) {
            m_source = param;
            emit sourceChanged(param);
        }
        return;
    }
    if (line.startsWith(QLatin1String("MSQUICK"))) {
        const int index = line.mid(7).trimmed().toInt();
        noteQuickSelectSlot(index);
        if (m_quickSelect != index) {
            m_quickSelect = index;
            emit quickSelectChanged(index);
        }
        return;
    }
    if (line.startsWith(QLatin1String("MS"))) {
        const QString mode = line.mid(2).trimmed();
        if (m_soundMode != mode) {
            m_soundMode = mode;
            emit soundModeChanged(mode);
        }
        return;
    }
    if (line.startsWith(QLatin1String("ECO"))) {
        const QString value = line.mid(3).trimmed();
        if ((value == QLatin1String("ON") || value == QLatin1String("OFF")
             || value == QLatin1String("AUTO"))
            && m_eco != value) {
            m_eco = value;
            emit ecoChanged(value);
        }
        return;
    }
    if (line.startsWith(QLatin1String("SLP"))) {
        const QString value = line.mid(3).trimmed();
        const int minutes = (value == QLatin1String("OFF")) ? 0 : value.toInt();
        if (m_sleepMinutes != minutes) {
            m_sleepMinutes = minutes;
            emit sleepChanged(minutes);
        }
        return;
    }
}

bool AvrState::handleZone(int zone, const QString &line)
{
    const QString body = line.mid(2);
    ExtraZone &z = extra(zone);
    if (!z.present) {
        z.present = true;
        emit zonePresenceChanged(zone);
    }

    // Zones 2 and 3 reuse the master prefixes for their own parameters; those
    // are not modelled here and must not be mistaken for an input name.
    static const QStringList ignoredPrefixes = {
        QStringLiteral("PS"), QStringLiteral("CV"), QStringLiteral("CS"),
        QStringLiteral("SLP"), QStringLiteral("HPF"), QStringLiteral("QUICK"),
        QStringLiteral("CH"),
    };
    for (const QString &prefix : ignoredPrefixes) {
        if (body.startsWith(prefix))
            return true;
    }

    if (body == QLatin1String("ON") || body == QLatin1String("OFF")) {
        const bool on = body == QLatin1String("ON");
        if (z.power != on) {
            z.power = on;
            emit zonePowerChanged(zone);
        }
        return true;
    }
    if (body == QLatin1String("MUON") || body == QLatin1String("MUOFF")) {
        const bool muted = body == QLatin1String("MUON");
        if (z.mute != muted) {
            z.mute = muted;
            emit zoneMuteChanged(zone);
        }
        return true;
    }

    double value = 0.0;
    if (parseVolume(body, &value)) {
        if (!qFuzzyCompare(z.volume, value)) {
            z.volume = value;
            emit zoneVolumeChanged(zone);
        }
        return true;
    }

    const QString param = body.trimmed();
    if (param.isEmpty())
        return true;
    if (param != QLatin1String("SOURCE"))
        noteSource(param);
    if (z.source != param) {
        z.source = param;
        emit zoneSourceChanged(zone);
    }
    return true;
}

bool AvrState::handleParameter(const QString &line)
{
    const QString body = line.mid(2);

    // The only key with a space in it, so it cannot go through the generic
    // "key value" split below.
    if (body.startsWith(QLatin1String("TONE CTRL "))) {
        const bool on = body.mid(10).trimmed() == QLatin1String("ON");
        if (m_toneControl != on) {
            m_toneControl = on;
            emit toneChanged();
        }
        return true;
    }

    const int space = body.indexOf(QLatin1Char(' '));
    if (space < 0)
        return false;
    const QString key = body.left(space);
    const QString value = body.mid(space + 1).trimmed();

    // A query for the subwoofer answers twice: whether level adjustment is
    // enabled at all, and only then the level itself. With it off the receiver
    // silently ignores any trim sent to it.
    if (key == QLatin1String("SWL")
        && (value == QLatin1String("ON") || value == QLatin1String("OFF"))) {
        const bool on = value == QLatin1String("ON");
        if (m_subwooferAdjust != on) {
            m_subwooferAdjust = on;
            emit toneChanged();
        }
        return true;
    }

    // Bass, treble and subwoofer trim are all reported centred on 50.
    if (key == QLatin1String("BAS") || key == QLatin1String("TRE")
        || key == QLatin1String("SWL")) {
        bool ok = false;
        const int offset = value.toInt(&ok) - 50;
        if (!ok)
            return true;
        int *target = (key == QLatin1String("BAS"))   ? &m_bass
                      : (key == QLatin1String("TRE")) ? &m_treble
                                                      : &m_subwoofer;
        if (*target != offset) {
            *target = offset;
            emit toneChanged();
        }
        return true;
    }
    if (key == QLatin1String("DRC")) {
        if (m_drc != value) {
            m_drc = value;
            emit drcChanged(value);
        }
        return true;
    }
    if (key == QLatin1String("RSTR")) {
        if (m_restorer != value) {
            m_restorer = value;
            emit restorerChanged(value);
        }
        return true;
    }
    return false;
}

bool AvrState::handleSetup(const QString &line)
{
    // "SSQSNZMAQS1 RMF FM" - the name given to a main-zone Quick Select slot.
    // The slot counts even with no name: it is how a model with five shows it.
    if (line.startsWith(QLatin1String("SSQSNZMA"))) {
        const QString body = line.mid(8).trimmed();
        if (body.size() < 3 || !body.startsWith(QLatin1String("QS")) || !body.at(2).isDigit())
            return true;
        const int slot = body.at(2).digitValue();
        const QString name = body.mid(3).trimmed();
        noteQuickSelectSlot(slot);
        if (!name.isEmpty() && m_quickSelectNames.value(slot) != name) {
            m_quickSelectNames.insert(slot, name);
            emit quickSelectNamesChanged();
        }
        return true;
    }

    // "SSFUNSAT/CBL CBL/SAT" - protocol name, then the label configured on the
    // receiver. "SSSODDVD USE" - whether that input is still enabled.
    const bool isFunctionName = line.startsWith(QLatin1String("SSFUN"));
    const bool isSourceDelete = line.startsWith(QLatin1String("SSSOD"));
    if (!isFunctionName && !isSourceDelete)
        return false;

    // Any answer at all, even a bare END, makes telnet the source of the
    // input list from now on.
    if (!m_catalogueReported) {
        m_catalogueReported = true;
        if (m_haveWebSources)
            emit sourceCatalogChanged();
    }

    const QString body = line.mid(5).trimmed();
    if (body.isEmpty() || body == QLatin1String("END"))
        return true;

    const int space = body.indexOf(QLatin1Char(' '));
    if (space < 0)
        return true;
    const QString param = body.left(space);
    const QString value = body.mid(space + 1).trimmed();
    if (param.isEmpty())
        return true;

    if (isFunctionName) {
        m_availableSources.insert(param);
        if (!value.isEmpty())
            m_sourceLabels.insert(param, value);
    } else {
        if (value == QLatin1String("DEL")) {
            m_deletedSources.insert(param);
        } else {
            m_deletedSources.remove(param);
            m_availableSources.insert(param);
        }
    }
    emit sourceCatalogChanged();
    return true;
}

void AvrState::noteQuickSelectSlot(int slot)
{
    if (slot <= m_highestQuickSlot)
        return;
    m_highestQuickSlot = slot;
    emit quickSelectNamesChanged();
}

void AvrState::noteSource(const QString &param)
{
    if (param.isEmpty() || m_availableSources.contains(param))
        return;
    m_availableSources.insert(param);
    emit sourceCatalogChanged();
}
