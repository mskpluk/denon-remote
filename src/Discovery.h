#pragma once

#include <QObject>
#include <QSet>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QUdpSocket;

struct DiscoveredDevice {
    QString host;
    QString model;
    QString mac;
};

// Finds controllable receivers on the local network.
//
// SSDP alone only says "something answered", so every responder is then asked
// for /goform/Deviceinfo.xml: a receiver that serves it is one this app can
// actually drive, which keeps unrelated UPnP devices out of the list.
class Discovery : public QObject
{
    Q_OBJECT

public:
    explicit Discovery(QObject *parent = nullptr);

    bool isRunning() const { return m_running; }

public slots:
    void start(int timeoutMs = 4000);
    void stop();

signals:
    void deviceFound(const DiscoveredDevice &device);
    void finished();

private slots:
    void readResponses();
    void onSearchTimeout();
    void onProbeFinished(QNetworkReply *reply);

private:
    void sendSearch();
    void probe(const QString &host);
    void finishIfIdle();

    QUdpSocket *m_socket;
    QNetworkAccessManager *m_network;
    QTimer *m_timeout;
    QTimer *m_repeat;
    QSet<QString> m_probed;
    int m_pendingProbes = 0;
    bool m_running = false;
    bool m_searchExpired = false;
};
