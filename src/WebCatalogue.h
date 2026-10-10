#pragma once

#include "AvrState.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

#include <optional>

class AvrConnection;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// Reads the input list from the receiver's web page, for models that predate
// the SSFUN/SSSOD telnet queries.
//
// Two layouts are in the field, both checked against captures from real
// receivers (the denonavr library's test data):
//  - up to about 2012 (AVR-1912, AVR-3312, ...) formMainZone_MainZoneXml.xml
//    lists the inputs under their protocol names, "SAT/CBL" or "NET/USB";
//  - 2013 to 2015 (AVR-X2000, AVR-X2100W, NR1604, SR6011, ...) leave that file
//    without an input list and put it in formMainZone_MainZoneXmlStatus.xml,
//    under display names ("CBL/SAT", "Blu-ray") that the receiver would not
//    take back as an SI command, so those are translated.
// Later models answer SSFUN over telnet and refuse both files with 403 (an
// AVR-X1400H does), so the page is only asked for when telnet stays silent:
// once per connection, a few seconds after it opens.
class WebCatalogue : public QObject
{
    Q_OBJECT

public:
    WebCatalogue(AvrConnection *connection, AvrState *state, QObject *parent = nullptr);

    // Both exist for the tests.
    void setDelay(int milliseconds) { m_delayMs = milliseconds; }
    void setHttpPort(quint16 port) { m_httpPort = port; }

    // The three parallel lists out of either page. Empty when the page has no
    // input list at all, so an error page is never taken for a receiver
    // without inputs.
    static std::optional<QVector<WebSource>> parse(const QByteArray &xml);

private slots:
    void onStatusChanged();
    void startFetch();

private:
    void requestPage(int index);
    void onReply(QNetworkReply *reply, int index);
    void abortReply();

    // SSFUN goes out about 1.3 s after connecting, paced behind the other
    // state queries; this leaves room for the answers on top.
    int m_delayMs = 4000;
    quint16 m_httpPort = 80;

    AvrConnection *m_connection;
    AvrState *m_state;
    QTimer *m_timer;
    QNetworkAccessManager *m_network;
    QNetworkReply *m_reply = nullptr;
    QString m_host;  // host the pending request was made for
};
