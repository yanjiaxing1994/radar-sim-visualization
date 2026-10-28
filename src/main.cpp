#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QtDebug>

#include "http_server.h"

namespace {

bool parsePort(const QString &text, quint16 *port)
{
    bool ok = false;
    const uint value = text.toUInt(&ok);
    if (!ok || value == 0 || value > 65535)
        return false;

    *port = static_cast<quint16>(value);
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("radar_sim"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Radar simulation backend"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption verboseOption(
        QStringLiteral("verbose"),
        QStringLiteral("Enable debug-level logging."));
    const QCommandLineOption configOption(
        QStringLiteral("config"),
        QStringLiteral("Path to the JSON config file."),
        QStringLiteral("file"),
        QStringLiteral("config.json"));
    const QCommandLineOption httpPortOption(
        QStringLiteral("http-port"),
        QStringLiteral("HTTP listen port (default: 8080)."),
        QStringLiteral("port"),
        QStringLiteral("8080"));
    const QCommandLineOption wsPortOption(
        QStringLiteral("ws-port"),
        QStringLiteral("WebSocket listen port (default: 8081)."),
        QStringLiteral("port"),
        QStringLiteral("8081"));

    parser.addOption(verboseOption);
    parser.addOption(configOption);
    parser.addOption(httpPortOption);
    parser.addOption(wsPortOption);
    parser.process(app);

    const bool verbose = parser.isSet(verboseOption);
    QLoggingCategory::setFilterRules(verbose ? QStringLiteral("*.debug=true")
                                             : QStringLiteral("*.debug=false"));

    const QString configPath = parser.value(configOption);

    quint16 httpPort = 8080;
    quint16 wsPort = 8081;
    if (!parsePort(parser.value(httpPortOption), &httpPort)) {
        qCritical() << "Invalid --http-port:" << parser.value(httpPortOption);
        return 1;
    }
    if (!parsePort(parser.value(wsPortOption), &wsPort)) {
        qCritical() << "Invalid --ws-port:" << parser.value(wsPortOption);
        return 1;
    }
    if (httpPort == wsPort) {
        qCritical() << "HTTP and WebSocket ports must differ:" << httpPort;
        return 1;
    }

    qInfo().noquote() << QStringLiteral("Qt version: %1 (runtime %2)")
                             .arg(QString::fromLatin1(QT_VERSION_STR),
                                  QString::fromLatin1(qVersion()));
    qInfo().noquote() << QStringLiteral("Config file: %1").arg(configPath);
    qInfo().noquote() << QStringLiteral("Verbose logging: %1")
                             .arg(verbose ? QStringLiteral("on") : QStringLiteral("off"));

    HttpServer server(configPath);
    if (!server.listen(QHostAddress::Any, httpPort)) {
        qCritical() << "HTTP listen on" << httpPort
                    << "failed:" << server.errorString();
        return 1;
    }
    qInfo().noquote() << QStringLiteral("HTTP listening on http://localhost:%1")
                             .arg(httpPort);

    if (!server.listenWebSocket(QHostAddress::Any, wsPort))
        return 1;

    return app.exec();
}
