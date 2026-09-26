#pragma once

#include <QByteArray>
#include <QObject>
#include <QQueue>
#include <QString>

class QTcpSocket;
class QTimer;

// Persistent telnet link (port 23) to a Denon/Marantz network receiver.
//
// The AVR pushes every state change over this socket on its own, so one
// long-lived connection doubles as the event source: turning the front panel
// knob, using the IR remote or the HEOS app all show up here without polling.
class AvrConnection : public QObject
{
    Q_OBJECT

public:
    enum class Status { Disconnected, Connecting, Connected };
    Q_ENUM(Status)

    explicit AvrConnection(QObject *parent = nullptr);

    Status status() const { return m_status; }
    QString host() const { return m_host; }
    bool isConnected() const { return m_status == Status::Connected; }

public slots:
    // Opens the link and keeps it up: drops are retried with a capped backoff
    // until close() is called.
    void open(const QString &host, quint16 port = 23);
    void close();

    // Commands are queued and paced; the protocol needs a gap between them.
    void send(const QString &command);

    // Asks for every value the UI mirrors, including the source catalogue.
    void refreshAll();

signals:
    void statusChanged(AvrConnection::Status status);
    void lineReceived(const QString &line);
    void errorOccurred(const QString &message);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError();
    void flushQueue();
    void attemptReconnect();

private:
    void setStatus(Status status);
    void scheduleReconnect();
    void enableTcpKeepAlive();

    // The AVR drops commands sent back to back; Denon's own documentation asks
    // for at least 50 ms of spacing.
    static constexpr int PaceIntervalMs = 80;
    static constexpr int MinBackoffMs = 2000;
    static constexpr int MaxBackoffMs = 30000;

    QTcpSocket *m_socket;
    QTimer *m_paceTimer;
    QTimer *m_reconnectTimer;
    QQueue<QString> m_queue;
    QByteArray m_buffer;
    QString m_host;
    quint16 m_port = 23;
    Status m_status = Status::Disconnected;
    bool m_wantConnection = false;
    int m_backoffMs = MinBackoffMs;
};
