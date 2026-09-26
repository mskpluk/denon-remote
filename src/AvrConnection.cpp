#include "AvrConnection.h"

#include <QTcpSocket>
#include <QTimer>

#ifdef Q_OS_LINUX
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

AvrConnection::AvrConnection(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_paceTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
{
    m_paceTimer->setInterval(PaceIntervalMs);
    m_reconnectTimer->setSingleShot(true);

    connect(m_socket, &QTcpSocket::connected, this, &AvrConnection::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &AvrConnection::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &AvrConnection::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &AvrConnection::onSocketError);

    connect(m_paceTimer, &QTimer::timeout, this, &AvrConnection::flushQueue);
    connect(m_reconnectTimer, &QTimer::timeout, this, &AvrConnection::attemptReconnect);
}

void AvrConnection::open(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port;
    m_wantConnection = true;
    m_backoffMs = MinBackoffMs;
    m_reconnectTimer->stop();

    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();

    m_queue.clear();
    m_buffer.clear();
    setStatus(Status::Connecting);
    m_socket->connectToHost(m_host, m_port);
}

void AvrConnection::close()
{
    m_wantConnection = false;
    m_reconnectTimer->stop();
    m_paceTimer->stop();
    m_queue.clear();
    m_buffer.clear();
    m_socket->abort();
    setStatus(Status::Disconnected);
}

void AvrConnection::send(const QString &command)
{
    if (command.isEmpty())
        return;

    m_queue.enqueue(command);
    if (isConnected() && !m_paceTimer->isActive()) {
        // Send the first one straight away so single clicks stay snappy; the
        // timer only exists to space out whatever follows.
        flushQueue();
        m_paceTimer->start();
    }
}

void AvrConnection::refreshAll()
{
    // SSFUN maps protocol source names to the labels configured on the AVR and
    // SSSOD says which of them are still enabled, so the source list is read
    // from the device rather than hard-coded.
    const QStringList queries = {
        QStringLiteral("PW?"),          QStringLiteral("MV?"),
        QStringLiteral("MU?"),          QStringLiteral("SI?"),
        QStringLiteral("MS?"),          QStringLiteral("MSQUICK ?"),
        QStringLiteral("PSBAS ?"),      QStringLiteral("PSTRE ?"),
        QStringLiteral("PSTONE CTRL ?"),QStringLiteral("PSSWL ?"),
        QStringLiteral("PSDRC ?"),      QStringLiteral("PSRSTR ?"),
        QStringLiteral("ECO?"),         QStringLiteral("SLP?"),
        QStringLiteral("Z2?"),          QStringLiteral("Z2MU?"),
        QStringLiteral("SSFUN ?"),      QStringLiteral("SSSOD ?"),
    };
    for (const QString &query : queries)
        send(query);
}

void AvrConnection::onConnected()
{
    m_backoffMs = MinBackoffMs;
    enableTcpKeepAlive();
    setStatus(Status::Connected);
    refreshAll();
}

void AvrConnection::onDisconnected()
{
    m_paceTimer->stop();
    m_queue.clear();
    setStatus(Status::Disconnected);
    scheduleReconnect();
}

void AvrConnection::onReadyRead()
{
    m_buffer.append(m_socket->readAll());

    // Replies are carriage-return terminated and several of them usually
    // arrive in one packet.
    int end;
    while ((end = m_buffer.indexOf('\r')) >= 0) {
        const QByteArray raw = m_buffer.left(end);
        m_buffer.remove(0, end + 1);
        const QString line = QString::fromLatin1(raw).trimmed();
        if (!line.isEmpty())
            emit lineReceived(line);
    }

    // A reply that never terminates would otherwise grow without bound.
    if (m_buffer.size() > 4096)
        m_buffer.clear();
}

void AvrConnection::onSocketError()
{
    if (m_wantConnection)
        emit errorOccurred(m_socket->errorString());

    if (m_status != Status::Disconnected) {
        m_paceTimer->stop();
        m_queue.clear();
        setStatus(Status::Disconnected);
    }
    scheduleReconnect();
}

void AvrConnection::flushQueue()
{
    if (!isConnected()) {
        m_paceTimer->stop();
        return;
    }
    if (m_queue.isEmpty()) {
        m_paceTimer->stop();
        return;
    }

    const QString command = m_queue.dequeue();
    m_socket->write(command.toLatin1() + '\r');
}

void AvrConnection::attemptReconnect()
{
    if (!m_wantConnection)
        return;

    setStatus(Status::Connecting);
    m_socket->abort();
    m_socket->connectToHost(m_host, m_port);
}

void AvrConnection::enableTcpKeepAlive()
{
    // A dead link (receiver unplugged, network gone) has to be noticed without
    // talking to the receiver: a command, even a query, may count as activity
    // and hold off its auto-standby. Keepalive probes are answered by the TCP
    // stack and never reach the control protocol. The system default waits two
    // hours before the first probe, so the timings are shortened to about 90 s.
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
#ifdef Q_OS_LINUX
    const int fd = int(m_socket->socketDescriptor());
    const int idleSeconds = 60;
    const int intervalSeconds = 10;
    const int probes = 3;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idleSeconds, sizeof idleSeconds);
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intervalSeconds, sizeof intervalSeconds);
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &probes, sizeof probes);
#endif
}

void AvrConnection::setStatus(Status status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged(status);
}

void AvrConnection::scheduleReconnect()
{
    if (!m_wantConnection || m_reconnectTimer->isActive())
        return;

    m_reconnectTimer->start(m_backoffMs);
    m_backoffMs = qMin(m_backoffMs * 2, MaxBackoffMs);
}
