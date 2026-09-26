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
    m_source.clear();
    m_zone2Source.clear();
    m_soundMode.clear();
    emit sourceCatalogChanged();
}

QVector<SourceEntry> AvrState::sources() const
{
    QVector<SourceEntry> result;
    QSet<QString> emitted;

    // Before the AVR has answered SSFUN/SSSOD there is nothing to filter
    // against, so the full canonical list stands in.
    const bool haveCatalogue = !m_availableSources.isEmpty();

    for (const CanonicalSource &candidate : kCanonicalSources) {
        const QString param = QString::fromLatin1(candidate.param);
        if (m_deletedSources.contains(param))
            continue;
        if (haveCatalogue && !candidate.alwaysAvailable && !m_availableSources.contains(param))
            continue;
        result.append({param, m_sourceLabels.value(param, QString::fromLatin1(candidate.label))});
        emitted.insert(param);
    }

    // Inputs this build has never heard of still belong in the list.
    QStringList extras;
    for (const QString &param : m_availableSources) {
        if (!emitted.contains(param) && !m_deletedSources.contains(param))
            extras.append(param);
    }
    extras.sort();
    for (const QString &param : std::as_const(extras))
        result.append({param, m_sourceLabels.value(param, param)});

    return result;
}

QVector<SourceEntry> AvrState::zone2Sources() const
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
    if (m_sourceLabels.contains(param))
        return m_sourceLabels.value(param);
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
        if (handleZone2(line))
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

bool AvrState::handleZone2(const QString &line)
{
    const QString body = line.mid(2);

    // Zone 2 reuses the master prefixes for its own parameters; those are not
    // modelled here and must not be mistaken for an input name.
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
        if (m_zone2Power != on) {
            m_zone2Power = on;
            emit zone2PowerChanged(on);
        }
        return true;
    }
    if (body == QLatin1String("MUON") || body == QLatin1String("MUOFF")) {
        const bool muted = body == QLatin1String("MUON");
        if (m_zone2Mute != muted) {
            m_zone2Mute = muted;
            emit zone2MuteChanged(muted);
        }
        return true;
    }

    double value = 0.0;
    if (parseVolume(body, &value)) {
        if (!qFuzzyCompare(m_zone2Volume, value)) {
            m_zone2Volume = value;
            emit zone2VolumeChanged(value);
        }
        return true;
    }

    const QString param = body.trimmed();
    if (param.isEmpty())
        return true;
    if (param != QLatin1String("SOURCE"))
        noteSource(param);
    if (m_zone2Source != param) {
        m_zone2Source = param;
        emit zone2SourceChanged(param);
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
    // "SSFUNSAT/CBL CBL/SAT" - protocol name, then the label configured on the
    // receiver. "SSSODDVD USE" - whether that input is still enabled.
    const bool isFunctionName = line.startsWith(QLatin1String("SSFUN"));
    const bool isSourceDelete = line.startsWith(QLatin1String("SSSOD"));
    if (!isFunctionName && !isSourceDelete)
        return false;

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

void AvrState::noteSource(const QString &param)
{
    if (param.isEmpty() || m_availableSources.contains(param))
        return;
    m_availableSources.insert(param);
    emit sourceCatalogChanged();
}
