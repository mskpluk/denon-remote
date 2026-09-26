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
            probe(host);
    }
}

void Discovery::probe(const QString &host)
{
    if (m_probed.contains(host))
        return;
    m_probed.insert(host);

    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(host);
    url.setPort(8080);
    url.setPath(QStringLiteral("/goform/Deviceinfo.xml"));

    QNetworkRequest request(url);
    request.setTransferTimeout(ProbeTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    ++m_pendingProbes;
    m_network->get(request);
}

void Discovery::onProbeFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    --m_pendingProbes;

    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray body = reply->readAll();
        QXmlStreamReader xml(body);
        DiscoveredDevice device;
        device.host = reply->url().host();

        while (!xml.atEnd() && !xml.hasError()) {
            if (xml.readNext() != QXmlStreamReader::StartElement)
                continue;
            if (xml.name() == QLatin1String("ModelName"))
                device.model = xml.readElementText().trimmed();
            else if (xml.name() == QLatin1String("MacAddress"))
                device.mac = xml.readElementText().trimmed();
        }

        // No model name means the XML came from something else entirely.
        if (!device.model.isEmpty())
            emit deviceFound(device);
    }

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
