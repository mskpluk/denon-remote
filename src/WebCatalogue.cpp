#include "WebCatalogue.h"

#include "AvrConnection.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>

namespace {

const char *const kPages[] = {
    "/goform/formMainZone_MainZoneXml.xml?ZoneName=ZONE1",
    "/goform/formMainZone_MainZoneXmlStatus.xml",
};
constexpr int kPageCount = 2;

constexpr int kTimeoutMs = 3000;
constexpr qint64 kMaxBytes = 256 * 1024;

// The 2013-2015 page names inputs the way the front panel does. SSFUN on
// later models reports the same inputs under the names on the right.
const QHash<QString, QString> &displayToParam()
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("CBL/SAT"), QStringLiteral("SAT/CBL")},
        {QStringLiteral("Blu-ray"), QStringLiteral("BD")},
        {QStringLiteral("TV AUDIO"), QStringLiteral("TV")},
        {QStringLiteral("Media Player"), QStringLiteral("MPLAY")},
        {QStringLiteral("iPod/USB"), QStringLiteral("USB/IPOD")},
        {QStringLiteral("NETWORK"), QStringLiteral("NET")},
        {QStringLiteral("Bluetooth"), QStringLiteral("BT")},
        {QStringLiteral("AUX"), QStringLiteral("AUX1")},
        {QStringLiteral("FM"), QStringLiteral("TUNER")},
        {QStringLiteral("Tuner"), QStringLiteral("TUNER")},
    };
    return map;
}

QString decodeEntities(const QString &text)
{
    if (!text.contains(QLatin1Char('&')))
        return text;
    static const QRegularExpression entity(QStringLiteral("&(#[xX]?[0-9A-Fa-f]+|[a-z]+);"));
    static const QHash<QString, QString> named = {
        {QStringLiteral("amp"), QStringLiteral("&")},   {QStringLiteral("lt"), QStringLiteral("<")},
        {QStringLiteral("gt"), QStringLiteral(">")},    {QStringLiteral("quot"), QStringLiteral("\"")},
        {QStringLiteral("apos"), QStringLiteral("'")},
    };

    QString result;
    qsizetype last = 0;
    auto it = entity.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        result += text.mid(last, m.capturedStart() - last);
        const QString name = m.captured(1);
        QString replacement = m.captured(0);
        if (name.startsWith(QLatin1Char('#'))) {
            bool ok = false;
            const bool hex = name.size() > 1 && (name.at(1) == QLatin1Char('x') || name.at(1) == QLatin1Char('X'));
            const uint cp = hex ? name.mid(2).toUInt(&ok, 16) : name.mid(1).toUInt(&ok, 10);
            if (ok && cp > 0 && cp <= 0x10FFFF && (cp < 0xD800 || cp > 0xDFFF)) {
                const char32_t c = cp;
                replacement = QString::fromUcs4(&c, 1);
            }
        } else if (named.contains(name)) {
            replacement = named.value(name);
        }
        result += replacement;
        last = m.capturedEnd();
    }
    result += text.mid(last);
    return result;
}

// The entries of one list, trimmed: "<Tag><value>A</value>...</Tag>", where
// newer firmware wraps each entry once more as "<value><value>A</value></value>".
// Matching only the innermost value covers both, and an empty entry still
// takes its place, so the lists stay aligned by position.
//
// Plain pattern matching rather than an XML parser: the page comes
// unauthenticated from the network, and nothing here ever resolves a DOCTYPE
// or an external entity.
std::optional<QStringList> values(const QString &xml, const QString &tag)
{
    const QRegularExpression blockPattern(
        QStringLiteral("<%1(?:\\s[^>]*)?>(.*?)</%1>").arg(tag),
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch block = blockPattern.match(xml);
    if (!block.hasMatch())
        return std::nullopt;

    static const QRegularExpression value(QStringLiteral("<value\\s*/>|<value>([^<]*)</value>"));
    QStringList result;
    auto it = value.globalMatch(block.captured(1));
    while (it.hasNext())
        result.append(decodeEntities(it.next().captured(1)).trimmed());
    return result;
}

} // namespace

WebCatalogue::WebCatalogue(AvrConnection *connection, AvrState *state, QObject *parent)
    : QObject(parent)
    , m_connection(connection)
    , m_state(state)
    , m_timer(new QTimer(this))
    , m_network(new QNetworkAccessManager(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &WebCatalogue::startFetch);
    connect(m_connection, &AvrConnection::statusChanged, this, &WebCatalogue::onStatusChanged);
}

std::optional<QVector<WebSource>> WebCatalogue::parse(const QByteArray &xml)
{
    // The pages declare UTF-8; the receiver's own names are ASCII.
    const QString text = QString::fromUtf8(xml);
    const std::optional<QStringList> functions = values(text, QStringLiteral("InputFuncList"));
    if (!functions)
        return std::nullopt;
    const QStringList renames = values(text, QStringLiteral("RenameSource")).value_or(QStringList());
    const QStringList deletes = values(text, QStringLiteral("SourceDelete")).value_or(QStringList());

    QVector<WebSource> result;
    QSet<QString> seen;
    for (qsizetype i = 0; i < functions->size(); ++i) {
        const QString &name = functions->at(i);
        // SOURCE is zone 2 following the main zone, not an input.
        if (name.isEmpty() || name == QLatin1String("SOURCE"))
            continue;
        const QString param = displayToParam().value(name, name);
        if (seen.contains(param))
            continue;
        seen.insert(param);
        WebSource source;
        source.param = param;
        source.label = renames.value(i);
        // Only DEL hides an input. An empty entry is one that cannot be
        // hidden at all, such as the network input.
        source.enabled = deletes.value(i) != QLatin1String("DEL");
        result.append(source);
    }
    return result;
}

void WebCatalogue::onStatusChanged()
{
    // AvrConnection reconnects on its own, so every new connection gets one
    // attempt and anything still pending for the old one is dropped.
    abortReply();
    m_timer->stop();
    if (m_connection->isConnected())
        m_timer->start(m_delayMs);
}

void WebCatalogue::startFetch()
{
    if (!m_connection->isConnected() || m_state->catalogueReported())
        return;
    m_host = m_connection->host();
    requestPage(0);
}

void WebCatalogue::requestPage(int index)
{
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(m_host);
    url.setPort(m_httpPort);
    const QString page = QString::fromLatin1(kPages[index]);
    const qsizetype query = page.indexOf(QLatin1Char('?'));
    url.setPath(page.left(query));
    if (query >= 0)
        url.setQuery(page.mid(query + 1));

    QNetworkRequest request(url);
    request.setTransferTimeout(kTimeoutMs);
    // Newer firmware redirects to HTTPS and then refuses anyway.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    m_reply = m_network->get(request);
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64) {
        if (received > kMaxBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, index] { onReply(reply, index); });
}

void WebCatalogue::onReply(QNetworkReply *reply, int index)
{
    reply->deleteLater();
    if (reply != m_reply)
        return;
    m_reply = nullptr;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    std::optional<QVector<WebSource>> sources;
    if (reply->error() == QNetworkReply::NoError && status == 200)
        sources = parse(reply->readAll());

    // The connection may have moved on, or SSFUN answered late after all.
    if (!m_connection->isConnected() || m_connection->host() != m_host || m_state->catalogueReported())
        return;
    if (sources) {
        m_state->setWebSources(*sources);
        return;
    }
    if (index + 1 < kPageCount)
        requestPage(index + 1);
}

void WebCatalogue::abortReply()
{
    if (!m_reply)
        return;
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->abort();
}
