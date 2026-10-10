#include "Discovery.h"

#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>
#include <QXmlStreamReader>

namespace {

constexpr quint16 SsdpPort = 1900;
constexpr int ProbeTimeoutMs = 2500;
const char kSsdpGroup[] = "239.255.255.250";

const char kSearchRequest[] =
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    "MAN: \"ssdp:discover\"\r\n"
    "MX: 2\r\n"
    "ST: ssdp:all\r\n"
    "\r\n";

QUrl deviceInfoUrl(const QString &host, quint16 port)
{
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(host);
    url.setPort(port);
    url.setPath(QStringLiteral("/goform/Deviceinfo.xml"));
    return url;
}

// "LOCATION: http://192.168.1.20:8080/description.xml" from an SSDP answer.
QUrl locationHeader(const QByteArray &payload)
{
    for (const QByteArray &line : payload.split('\n')) {
        const int colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().compare("LOCATION", Qt::CaseInsensitive) == 0)
            return QUrl(QString::fromLatin1(line.mid(colon + 1).trimmed()));
    }
    return QUrl();
}

} // namespace

Discovery::Discovery(QObject *parent)
    : QObject(parent)
    , m_socket(new QUdpSocket(this))
    , m_network(new QNetworkAccessManager(this))
    , m_timeout(new QTimer(this))
    , m_repeat(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    m_repeat->setInterval(900);

    connect(m_socket, &QUdpSocket::readyRead, this, &Discovery::readResponses);
    connect(m_timeout, &QTimer::timeout, this, &Discovery::onSearchTimeout);
    connect(m_repeat, &QTimer::timeout, this, &Discovery::sendSearch);
    connect(m_network, &QNetworkAccessManager::finished, this, &Discovery::onProbeFinished);
}

void Discovery::start(int timeoutMs)
{
    if (m_running)
        return;

    m_running = true;
    m_searchExpired = false;
    m_pendingProbes = 0;
    m_probed.clear();
    m_probeQueue.clear();
    m_descriptions.clear();
    m_descriptionsQueued.clear();

    m_socket->close();
    if (!m_socket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress)) {
        m_running = false;
        emit finished();
        return;
    }
    m_socket->setSocketOption(QAbstractSocket::MulticastTtlOption, 4);

    sendSearch();
    m_repeat->start();
    m_timeout->start(timeoutMs);
}

void Discovery::stop()
{
    if (!m_running)
        return;
    m_running = false;
    m_repeat->stop();
    m_timeout->stop();
    m_socket->close();
    emit finished();
}

void Discovery::sendSearch()
{
    m_socket->writeDatagram(QByteArray(kSearchRequest), QHostAddress(QLatin1String(kSsdpGroup)),
                            SsdpPort);
}

void Discovery::readResponses()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray payload(int(m_socket->pendingDatagramSize()), Qt::Uninitialized);
        QHostAddress sender;
        m_socket->readDatagram(payload.data(), payload.size(), &sender);

        // Any responder is a candidate; the HTTP probe decides whether it is
        // a receiver this app can control.
        bool convertedFromIPv6 = false;
        const QHostAddress address(sender.toIPv4Address(&convertedFromIPv6));
        const QString host = convertedFromIPv6 ? address.toString() : sender.toString();
        if (!host.isEmpty())
            probe(host, locationHeader(payload));
    }
}

void Discovery::setProbePorts(quint16 current, quint16 older)
{
    m_currentPort = current;
    m_olderPort = older;
}

void Discovery::probe(const QString &host, const QUrl &description)
{
    // Only a description on the answering host itself is worth fetching.
    if (description.isValid() && description.scheme() == QLatin1String("http")
        && description.host() == host) {
        QList<QUrl> &known = m_descriptions[host];
        if (!known.contains(description))
            known.append(description);
    }

    if (m_probed.contains(host))
        return;
    m_probed.insert(host);
    m_probeQueue.insert(host, {deviceInfoUrl(host, m_currentPort), deviceInfoUrl(host, m_olderPort)});
    ++m_pendingProbes;
    probeNext(host);
}

void Discovery::probeNext(const QString &host)
{
    QList<QUrl> &queue = m_probeQueue[host];
    // The descriptions are queued last and only now, so that ones arriving in
    // later SSDP answers still make it in.
    if (queue.isEmpty() && !m_descriptionsQueued.contains(host)) {
        m_descriptionsQueued.insert(host);
        queue = m_descriptions.value(host);
    }
    if (queue.isEmpty()) {
        m_probeQueue.remove(host);
        --m_pendingProbes;
        finishIfIdle();
        return;
    }

    QNetworkRequest request(queue.takeFirst());
    request.setTransferTimeout(ProbeTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network->get(request);
    reply->setProperty("probeHost", host);
    reply->setProperty("probeDescription", m_descriptionsQueued.contains(host));
}

DiscoveredDevice Discovery::parseDeviceInfo(const QByteArray &xml)
{
    QXmlStreamReader reader(xml);
    DiscoveredDevice device;
    while (!reader.atEnd() && !reader.hasError()) {
        if (reader.readNext() != QXmlStreamReader::StartElement)
            continue;
        if (reader.name() == QLatin1String("ModelName"))
            device.model = reader.readElementText().trimmed();
        else if (reader.name() == QLatin1String("MacAddress"))
            device.mac = reader.readElementText().trimmed();
    }
    // The 2013-2015 models write the name as "*AVR-X2000".
    if (device.model.startsWith(QLatin1Char('*')))
        device.model = device.model.mid(1).trimmed();
    return device;
}

DiscoveredDevice Discovery::parseDescription(const QByteArray &xml)
{
    // The first device in the description is the receiver itself; embedded
    // devices follow it.
    QXmlStreamReader reader(xml);
    QString manufacturer;
    QString model;
    while (!reader.atEnd() && !reader.hasError() && (manufacturer.isEmpty() || model.isEmpty())) {
        if (reader.readNext() != QXmlStreamReader::StartElement)
            continue;
        if (reader.name() == QLatin1String("manufacturer") && manufacturer.isEmpty())
            manufacturer = reader.readElementText().trimmed();
        else if (reader.name() == QLatin1String("modelName") && model.isEmpty())
            model = reader.readElementText().trimmed();
    }
    DiscoveredDevice device;
    if (manufacturer.contains(QLatin1String("denon"), Qt::CaseInsensitive)
        || manufacturer.contains(QLatin1String("marantz"), Qt::CaseInsensitive))
        device.model = model;
    return device;
}

void Discovery::onProbeFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    const QString host = reply->property("probeHost").toString();
    if (!m_probeQueue.contains(host))
        return;

    DiscoveredDevice device;
    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray body = reply->readAll();
        device = reply->property("probeDescription").toBool() ? parseDescription(body)
                                                              : parseDeviceInfo(body);
    }

    // No model name means the reply came from something else entirely.
    if (device.model.isEmpty()) {
        probeNext(host);
        return;
    }
    device.host = host;
    emit deviceFound(device);
    m_probeQueue.remove(host);
    --m_pendingProbes;
    finishIfIdle();
}

void Discovery::onSearchTimeout()
{
    m_searchExpired = true;
    m_repeat->stop();
    m_socket->close();
    finishIfIdle();
}

void Discovery::finishIfIdle()
{
    if (!m_running || !m_searchExpired || m_pendingProbes > 0)
        return;
    m_running = false;
    emit finished();
}
