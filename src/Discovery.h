#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

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
// what it is, keeping unrelated UPnP devices out of the list. Receivers from
// 2016 on serve /goform/Deviceinfo.xml on port 8080, those from 2013 to 2015
// the same file on port 80. Older ones have no such file; the UPnP device
// description their SSDP answer points to names the maker and the model.
class Discovery : public QObject
{
    Q_OBJECT

public:
    explicit Discovery(QObject *parent = nullptr);

    bool isRunning() const { return m_running; }

    // What one reply says about the device; an empty model means it is not
    // a receiver this app can drive.
    static DiscoveredDevice parseDeviceInfo(const QByteArray &xml);
    static DiscoveredDevice parseDescription(const QByteArray &xml);

    // For the tests: where the two Deviceinfo.xml probes go.
    void setProbePorts(quint16 current, quint16 older);

public slots:
    void start(int timeoutMs = 4000);
    void stop();
    // Identifies one host as if it had answered the search; description is
    // the LOCATION from its SSDP answer, if any.
    void probe(const QString &host, const QUrl &description = QUrl());

signals:
    void deviceFound(const DiscoveredDevice &device);
    void finished();

private slots:
    void readResponses();
    void onSearchTimeout();
    void onProbeFinished(QNetworkReply *reply);

private:
    void sendSearch();
    void probeNext(const QString &host);
    void finishIfIdle();

    QUdpSocket *m_socket;
    QNetworkAccessManager *m_network;
    QTimer *m_timeout;
    QTimer *m_repeat;
    QSet<QString> m_probed;
    QHash<QString, QList<QUrl>> m_probeQueue;   // host -> URLs still to try
    QHash<QString, QList<QUrl>> m_descriptions; // host -> LOCATIONs seen so far
    QSet<QString> m_descriptionsQueued;
    quint16 m_currentPort = 8080;
    quint16 m_olderPort = 80;
    int m_pendingProbes = 0;
    bool m_running = false;
    bool m_searchExpired = false;
};
