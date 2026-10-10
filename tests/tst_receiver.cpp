#include "AvrConnection.h"
#include "AvrState.h"
#include "Discovery.h"
#include "WebCatalogue.h"

#include <QFile>
#include <QHash>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

namespace {

QByteArray capture(const QString &name)
{
    QFile file(QStringLiteral(":/data/") + name);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing capture %s", qPrintable(name));
    return file.readAll();
}

QHash<QString, WebSource> byParam(const QVector<WebSource> &sources)
{
    QHash<QString, WebSource> result;
    for (const WebSource &s : sources)
        result.insert(s.param, s);
    return result;
}

QStringList params(const QVector<SourceEntry> &entries)
{
    QStringList result;
    for (const SourceEntry &e : entries)
        result.append(e.param);
    return result;
}

QStringList labels(const QVector<SourceEntry> &entries)
{
    QStringList result;
    for (const SourceEntry &e : entries)
        result.append(e.label);
    return result;
}

void feed(AvrState &state, const QStringList &lines)
{
    for (const QString &line : lines)
        state.handleLine(line);
}

// Telnet side of a receiver: answers SSFUN or stays silent, like an older model.
class FakeTelnet : public QTcpServer
{
public:
    explicit FakeTelnet(bool answersCatalogue)
        : m_answersCatalogue(answersCatalogue)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onRead(socket); });
            }
        });
        listen(QHostAddress::LocalHost);
    }

private:
    void onRead(QTcpSocket *socket)
    {
        m_buffer += socket->readAll();
        qsizetype end;
        while ((end = m_buffer.indexOf('\r')) >= 0) {
            const QByteArray command = m_buffer.left(end);
            m_buffer.remove(0, end + 1);
            if (command == "SI?")
                socket->write("SIDVD\r");
            else if (command == "SSFUN ?" && m_answersCatalogue)
                socket->write("SSFUNCD CD Player\rSSFUN END\r");
        }
    }

    bool m_answersCatalogue;
    QByteArray m_buffer;
};

// Web side: serves the given pages by path, refuses everything else.
class FakeWeb : public QTcpServer
{
public:
    explicit FakeWeb(const QHash<QString, QByteArray> &pages)
        : m_pages(pages)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = nextPendingConnection()) {
                ++connections;
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onRead(socket); });
            }
        });
        listen(QHostAddress::LocalHost);
    }

    int connections = 0;
    QStringList paths;

private:
    void onRead(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        buffer += socket->readAll();
        if (!buffer.contains("\r\n\r\n"))
            return;
        const QString path = QString::fromLatin1(buffer.split(' ').value(1));
        paths.append(path);
        if (m_pages.contains(path)) {
            const QByteArray page = m_pages.value(path);
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nContent-Length: "
                          + QByteArray::number(page.size()) + "\r\nConnection: close\r\n\r\n" + page);
        } else {
            socket->write("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        }
        socket->disconnectFromHost();
    }

    QHash<QString, QByteArray> m_pages;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

// A UPnP description as receivers from before 2013 serve it, trimmed to the
// fields that matter here.
QByteArray description(const char *manufacturer, const char *model)
{
    return QByteArray("<?xml version=\"1.0\"?>\n<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
                      "<device><deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>"
                      "<manufacturer>")
        + manufacturer + "</manufacturer><modelName>" + model
        + "</modelName><deviceList><device><manufacturer>Other</manufacturer>"
          "<modelName>Embedded</modelName></device></deviceList></device></root>";
}

const QString kMainZonePage = QStringLiteral("/goform/formMainZone_MainZoneXml.xml?ZoneName=ZONE1");
const QString kStatusPage = QStringLiteral("/goform/formMainZone_MainZoneXmlStatus.xml");

} // namespace

class TestReceiver : public QObject
{
    Q_OBJECT

private slots:
    // Reading the web pages

    void olderPageListsProtocolNames()
    {
        const auto sources = WebCatalogue::parse(capture(QStringLiteral("AVR-1912-formMainZone_MainZoneXml.xml")));
        QVERIFY(sources);
        QStringList names;
        for (const WebSource &s : *sources)
            names.append(s.param);
        QCOMPARE(names, (QStringList{"TUNER", "CD", "NET/USB", "DVD", "BD", "TV", "SAT/CBL",
                                     "DOCK", "GAME2", "GAME", "V.AUX"}));
        const auto map = byParam(*sources);
        QCOMPARE(map["DVD"].label, QStringLiteral("Chrome"));
        QCOMPARE(map["DOCK"].label, QStringLiteral("Chrome A"));
        QVERIFY(!map["CD"].enabled);
        QVERIFY(map["NET/USB"].enabled);
    }

    void zoneFollowEntryIsNotAnInput()
    {
        const auto sources = WebCatalogue::parse(capture(QStringLiteral("AVR-3311CI-formMainZone_MainZoneXml.xml")));
        QVERIFY(sources);
        QVERIFY(!byParam(*sources).contains(QStringLiteral("SOURCE")));
    }

    void emptyDeleteEntryCountsAsEnabled()
    {
        const auto map = byParam(*WebCatalogue::parse(capture(QStringLiteral("AVR-4810-formMainZone_MainZoneXml.xml"))));
        QVERIFY(map["XM"].enabled);
        QVERIFY(map["XM"].label.isEmpty());
        QVERIFY(!map["TUNER"].enabled);
        QCOMPARE(map["SAT/CBL"].label, QStringLiteral("WeTek"));
    }

    void newerMainZonePageHasNoInputList()
    {
        QVERIFY(!WebCatalogue::parse(capture(QStringLiteral("AVR-X2100W-formMainZone_MainZoneXml.xml"))));
    }

    void statusPageDisplayNamesBecomeProtocolNames()
    {
        const auto sources = WebCatalogue::parse(capture(QStringLiteral("SR6011-formMainZone_MainZoneXmlStatus.xml")));
        QVERIFY(sources);
        QStringList names;
        for (const WebSource &s : *sources)
            names.append(s.param);
        QCOMPARE(names, (QStringList{"SAT/CBL", "DVD", "BD", "GAME", "AUX1", "AUX2", "MPLAY",
                                     "USB/IPOD", "CD", "TUNER", "NET", "TV", "PHONO", "BT"}));
        const auto map = byParam(*sources);
        // Nested <value><value>, padded with spaces on the receiver.
        QCOMPARE(map["SAT/CBL"].label, QStringLiteral("CBL-Fetch"));
        QCOMPARE(map["MPLAY"].label, QStringLiteral("Media-Kodi"));
        // The network input cannot be hidden and leaves its entry empty.
        QVERIFY(map["NET"].enabled);
        QVERIFY(!map["BT"].enabled);
    }

    void refusalPageIsNotACatalogue()
    {
        // What an AVR-X1400H answers on both ports.
        QVERIFY(!WebCatalogue::parse("Error 403: Forbidden\r\nAccess Forbidden"));
    }

    void entitiesAreDecodedAndNothingElse()
    {
        const auto sources = WebCatalogue::parse(
            "<item><InputFuncList><value>CD</value><value>DVD</value></InputFuncList>"
            "<RenameSource><value>R&amp;B &#x41;&#66;</value><value>&unknown;</value></RenameSource></item>");
        QVERIFY(sources);
        QCOMPARE(sources->at(0).label, QStringLiteral("R&B AB"));
        QCOMPARE(sources->at(1).label, QStringLiteral("&unknown;"));
    }

    void selfClosingEntryKeepsListsAligned()
    {
        const auto sources = WebCatalogue::parse(
            "<item><InputFuncList><value>CD</value><value>DVD</value></InputFuncList>"
            "<RenameSource><value/><value>Player</value></RenameSource>"
            "<SourceDelete><value>USE</value><value>DEL</value></SourceDelete></item>");
        QVERIFY(sources);
        QVERIFY(sources->at(0).label.isEmpty());
        QCOMPARE(sources->at(1).label, QStringLiteral("Player"));
        QVERIFY(!sources->at(1).enabled);
    }

    // The input list the window shows

    void fullListBeforeTheCatalogueArrives()
    {
        AvrState state;
        QCOMPARE(state.sources().size(), 13);
    }

    void currentInputAloneIsNoCatalogue()
    {
        // A model that never answers SSFUN still reports its current input;
        // that one name must not shrink the list down to itself.
        AvrState state;
        feed(state, {"SIDVD", "Z2CD"});
        QCOMPARE(state.sources().size(), 13);
    }

    void catalogueComesFromTelnet()
    {
        AvrState state;
        feed(state, {"SSFUNCD CD", "SSFUNSAT/CBL Dekoder", "SSFUNGAME Konsola", "SSFUNDVD DVD",
                     "SSSODDVD DEL", "SSSODCD USE", "SSFUN END"});
        QCOMPARE(params(state.sources()), (QStringList{"CD", "SAT/CBL", "GAME", "BT"}));
        QCOMPARE(state.sources().at(1).label, QStringLiteral("Dekoder"));
    }

    void webListStandsInWhenTelnetHasNoCatalogue()
    {
        AvrState state;
        feed(state, {"SISAT/CBL"});
        state.setWebSources(*WebCatalogue::parse(capture(QStringLiteral("SR6011-formMainZone_MainZoneXmlStatus.xml"))));
        QCOMPARE(params(state.sources()),
                 (QStringList{"DVD", "BD", "TV", "SAT/CBL", "MPLAY", "GAME", "AUX1", "AUX2", "NET"}));
        QCOMPARE(state.sourceLabel(QStringLiteral("SAT/CBL")), QStringLiteral("CBL-Fetch"));
        QCOMPARE(state.sourceLabel(QStringLiteral("NET")), QStringLiteral("Online Music"));
    }

    void olderModelKeepsItsOwnInputsAndNoBluetooth()
    {
        AvrState state;
        state.setWebSources(*WebCatalogue::parse(capture(QStringLiteral("AVR-1912-formMainZone_MainZoneXml.xml"))));
        QCOMPARE(params(state.sources()), (QStringList{"TUNER", "DVD", "BD", "DOCK", "NET/USB"}));
        QCOMPARE(labels(state.sources()), (QStringList{"TUNER", "Chrome", "Kodi", "Chrome A", "NET/USB"}));
    }

    void telnetCatalogueOverridesTheWebPage()
    {
        AvrState state;
        state.setWebSources(*WebCatalogue::parse(capture(QStringLiteral("SR6011-formMainZone_MainZoneXmlStatus.xml"))));
        QSignalSpy changed(&state, &AvrState::sourceCatalogChanged);
        feed(state, {"SSFUNCD CD Player", "SSFUN END"});
        QVERIFY(changed.count() > 0);
        QCOMPARE(params(state.sources()), (QStringList{"CD", "BT"}));
        QCOMPARE(state.sourceLabel(QStringLiteral("SAT/CBL")), QStringLiteral("CBL/SAT"));
    }

    // Quick Select

    void quickSelectNamesFromAnX1400H()
    {
        // Exactly what an AVR-X1400H sends, trailing padding and all.
        AvrState state;
        feed(state, {"MSQUICK0", "SSQSNZMAQS1 RMF FM          ", "SSQSNZMAQS2 Radio ZET       ",
                     "SSQSNZMAQS3 Radio ESKA      ", "SSQSNZMAQS4 Muzyczne Radio  ", "SSQSNZMA END"});
        QCOMPARE(state.quickSelectSlots(), 4);
        QCOMPARE(state.quickSelectName(1), QStringLiteral("RMF FM"));
        QCOMPARE(state.quickSelectName(4), QStringLiteral("Muzyczne Radio"));
        QVERIFY(state.quickSelectName(5).isEmpty());
    }

    void fourSlotsUntilTheReceiverSaysOtherwise()
    {
        AvrState state;
        QCOMPARE(state.quickSelectSlots(), 4);
    }

    void fifthSlotShowsEvenWithoutAName()
    {
        AvrState state;
        feed(state, {"SSQSNZMAQS1 Radio", "SSQSNZMAQS5"});
        QCOMPARE(state.quickSelectSlots(), 5);
    }

    void fifthSlotRecalledFromThePanel()
    {
        AvrState state;
        feed(state, {"MSQUICK5"});
        QCOMPARE(state.quickSelectSlots(), 5);
        QCOMPARE(state.quickSelect(), 5);
    }

    void resetForgetsTheLastReceiver()
    {
        AvrState state;
        feed(state, {"SSQSNZMAQS5 Jazz", "SSFUNCD CD", "SSFUN END"});
        state.setWebSources({WebSource{QStringLiteral("DVD"), QString(), true}});
        state.reset();
        QCOMPARE(state.quickSelectSlots(), 4);
        QVERIFY(state.quickSelectName(5).isEmpty());
        QVERIFY(!state.catalogueReported());
        QCOMPARE(state.sources().size(), 13);
    }

    // Zone 3

    void zone3StaysAbsentOnAnX1400H()
    {
        // What an AVR-X1400H answers: zone 2, and silence for Z3?.
        AvrState state;
        feed(state, {"Z2OFF", "Z2SOURCE", "Z240"});
        QVERIFY(state.zonePresent(2));
        QVERIFY(!state.zonePresent(3));
    }

    void zone3AppearsOnceReported()
    {
        AvrState state;
        QSignalSpy presence(&state, &AvrState::zonePresenceChanged);
        feed(state, {"Z3ON", "Z345", "Z3CD", "Z3MUON"});
        QVERIFY(state.zonePresent(3));
        QCOMPARE(presence.count(), 1);
        QCOMPARE(presence.first().first().toInt(), 3);
        QVERIFY(state.zonePower(3));
        QCOMPARE(state.zoneVolume(3), 45.0);
        QCOMPARE(state.zoneSource(3), QStringLiteral("CD"));
        QVERIFY(state.zoneMute(3));
        // Zone 2 is untouched.
        QVERIFY(!state.zonePower(2));
        QVERIFY(state.zoneSource(2).isEmpty());
    }

    void zone3ParametersAreNotInputs()
    {
        AvrState state;
        feed(state, {"Z3PSBAS 50", "Z3SLPOFF"});
        QVERIFY(state.zonePresent(3));
        QVERIFY(state.zoneSource(3).isEmpty());
    }

    void resetHidesZone3AndTheOptions()
    {
        AvrState state;
        feed(state, {"Z3ON", "PSDRC LOW", "PSRSTR LOW", "ECOAUTO"});
        QCOMPARE(state.drc(), QStringLiteral("LOW"));
        state.reset();
        QVERIFY(!state.zonePresent(3));
        QVERIFY(state.drc().isEmpty());
        QVERIFY(state.restorer().isEmpty());
        QVERIFY(state.eco().isEmpty());
    }

    // Discovery

    void deviceInfoFromEachEra()
    {
        // 2016 on, port 8080.
        DiscoveredDevice d = Discovery::parseDeviceInfo(capture(QStringLiteral("AVC-X3700H-Deviceinfo-8080.xml")));
        QCOMPARE(d.model, QStringLiteral("AVC-X3700H"));
        // 2013-2015, port 80, with a star in front of the name.
        d = Discovery::parseDeviceInfo(capture(QStringLiteral("AVR-X2000-Deviceinfo.xml")));
        QCOMPARE(d.model, QStringLiteral("AVR-X2000"));
        // Older: an error page, not a device.
        d = Discovery::parseDeviceInfo(capture(QStringLiteral("AVR-1912-Deviceinfo.xml")));
        QVERIFY(d.model.isEmpty());
    }

    void descriptionNamesTheReceiver()
    {
        QCOMPARE(Discovery::parseDescription(description("DENON", "AVR-3310")).model, QStringLiteral("AVR-3310"));
        QCOMPARE(Discovery::parseDescription(description("Marantz", "SR6005")).model, QStringLiteral("SR6005"));
        QVERIFY(Discovery::parseDescription(description("Samsung Electronics", "UE55")).model.isEmpty());
    }

    void olderReceiverFoundOnPort80()
    {
        FakeWeb current({});
        FakeWeb older({{QStringLiteral("/goform/Deviceinfo.xml"), capture(QStringLiteral("AVR-X2000-Deviceinfo.xml"))}});
        FakeWeb upnp({{QStringLiteral("/description.xml"), description("DENON", "should-not-be-asked")}});
        Discovery discovery;
        discovery.setProbePorts(current.serverPort(), older.serverPort());
        QSignalSpy found(&discovery, &Discovery::deviceFound);

        discovery.probe(QStringLiteral("127.0.0.1"),
                        QUrl(QStringLiteral("http://127.0.0.1:%1/description.xml").arg(upnp.serverPort())));
        QTRY_COMPARE_WITH_TIMEOUT(found.count(), 1, 5000);
        QCOMPARE(found.first().first().value<DiscoveredDevice>().model, QStringLiteral("AVR-X2000"));
        QCOMPARE(upnp.connections, 0);
    }

    void oldestReceiverFoundByItsDescription()
    {
        // Nothing on 8080, an error page on 80, as an AVR-1912 serves it.
        FakeWeb current({});
        FakeWeb older({{QStringLiteral("/goform/Deviceinfo.xml"), capture(QStringLiteral("AVR-1912-Deviceinfo.xml"))}});
        FakeWeb upnp({{QStringLiteral("/description.xml"), description("DENON", "AVR-1912")}});
        Discovery discovery;
        discovery.setProbePorts(current.serverPort(), older.serverPort());
        QSignalSpy found(&discovery, &Discovery::deviceFound);

        discovery.probe(QStringLiteral("127.0.0.1"),
                        QUrl(QStringLiteral("http://127.0.0.1:%1/description.xml").arg(upnp.serverPort())));
        QTRY_COMPARE_WITH_TIMEOUT(found.count(), 1, 5000);
        const DiscoveredDevice d = found.first().first().value<DiscoveredDevice>();
        QCOMPARE(d.model, QStringLiteral("AVR-1912"));
        QCOMPARE(d.host, QStringLiteral("127.0.0.1"));
    }

    void otherDevicesStayOut()
    {
        FakeWeb current({});
        FakeWeb older({});
        FakeWeb upnp({{QStringLiteral("/description.xml"), description("Samsung Electronics", "UE55")}});
        Discovery discovery;
        discovery.setProbePorts(current.serverPort(), older.serverPort());
        QSignalSpy found(&discovery, &Discovery::deviceFound);

        discovery.probe(QStringLiteral("127.0.0.1"),
                        QUrl(QStringLiteral("http://127.0.0.1:%1/description.xml").arg(upnp.serverPort())));
        QTRY_COMPARE_WITH_TIMEOUT(upnp.connections, 1, 5000);
        QTest::qWait(300);
        QCOMPARE(found.count(), 0);
    }

    // The whole fallback, against fake receivers on the loopback

    void silentTelnetMakesTheAppAskTheWebPage()
    {
        FakeTelnet telnet(false);
        FakeWeb web({{kMainZonePage, capture(QStringLiteral("AVR-1912-formMainZone_MainZoneXml.xml"))}});
        AvrConnection connection;
        AvrState state;
        connect(&connection, &AvrConnection::lineReceived, &state, &AvrState::handleLine);
        WebCatalogue catalogue(&connection, &state);
        catalogue.setDelay(300);
        catalogue.setHttpPort(web.serverPort());

        connection.open(QStringLiteral("127.0.0.1"), telnet.serverPort());
        QTRY_COMPARE_WITH_TIMEOUT(params(state.sources()),
                                  (QStringList{"TUNER", "DVD", "BD", "DOCK", "NET/USB"}), 5000);
        QCOMPARE(web.connections, 1);
        QCOMPARE(web.paths, QStringList{kMainZonePage});
        connection.close();
    }

    void laterModelIsAskedForTheStatusPageToo()
    {
        // 2013-2015: the main zone page answers, but without an input list.
        FakeTelnet telnet(false);
        FakeWeb web({{kMainZonePage, capture(QStringLiteral("AVR-X2100W-formMainZone_MainZoneXml.xml"))},
                     {kStatusPage, capture(QStringLiteral("SR6011-formMainZone_MainZoneXmlStatus.xml"))}});
        AvrConnection connection;
        AvrState state;
        connect(&connection, &AvrConnection::lineReceived, &state, &AvrState::handleLine);
        WebCatalogue catalogue(&connection, &state);
        catalogue.setDelay(300);
        catalogue.setHttpPort(web.serverPort());

        connection.open(QStringLiteral("127.0.0.1"), telnet.serverPort());
        QTRY_COMPARE_WITH_TIMEOUT(params(state.sources()),
                                  (QStringList{"DVD", "BD", "TV", "SAT/CBL", "MPLAY", "GAME", "AUX1", "AUX2", "NET"}),
                                  5000);
        QCOMPARE(web.paths, (QStringList{kMainZonePage, kStatusPage}));
        connection.close();
    }

    void answeringTelnetMeansNoWebRequest()
    {
        FakeTelnet telnet(true);
        FakeWeb web({{kMainZonePage, capture(QStringLiteral("AVR-1912-formMainZone_MainZoneXml.xml"))}});
        AvrConnection connection;
        AvrState state;
        connect(&connection, &AvrConnection::lineReceived, &state, &AvrState::handleLine);
        WebCatalogue catalogue(&connection, &state);
        // SSFUN only goes out about 1.4 s in, behind the other state queries,
        // so the delay has to outlast the queue as the real one does.
        catalogue.setDelay(2500);
        catalogue.setHttpPort(web.serverPort());

        connection.open(QStringLiteral("127.0.0.1"), telnet.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(state.catalogueReported(), 5000);
        // Well past the delay: still nothing on the web side.
        QTest::qWait(3500);
        QCOMPARE(web.connections, 0);
        // DVD is the current input, which always stays listed.
        QCOMPARE(params(state.sources()), (QStringList{"CD", "DVD", "BT"}));
        connection.close();
    }
};

QTEST_GUILESS_MAIN(TestReceiver)
#include "tst_receiver.moc"
