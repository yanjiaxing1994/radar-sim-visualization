#include "http_server.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLocale>
#include <QMutexLocker>
#include <QSaveFile>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include <QWebSocketProtocol>
#include <QWebSocketServer>
#include <QtDebug>

#include "detection.h"
#include "types.h"

namespace {

constexpr int kMaxHeaderBytes = 16 * 1024;
constexpr qint64 kMaxBodyBytes = 1024 * 1024;
constexpr int kBroadcastIntervalMs = 50;

// 请求结束时打印方法、路径和耗时。
class ScopedRequestLog {
public:
    ScopedRequestLog(const QByteArray &method, const QByteArray &target)
        : m_method(method)
        , m_target(target)
    {
        m_timer.start();
    }

    ~ScopedRequestLog()
    {
        qInfo().noquote() << QStringLiteral("HTTP %1 %2 (%3 ms)")
                                 .arg(QString::fromLatin1(m_method),
                                      QString::fromLatin1(m_target))
                                 .arg(m_timer.elapsed());
    }

private:
    QByteArray m_method;
    QByteArray m_target;
    QElapsedTimer m_timer;
};

QByteArray statusText(int statusCode)
{
    switch (statusCode) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 415: return "Unsupported Media Type";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    default:  return "Internal Server Error";
    }
}

QByteArray headerValue(const QByteArray &requestHead, const QByteArray &name)
{
    const QList<QByteArray> lines = requestHead.split('\n');
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        if (line.left(colon).trimmed().compare(name, Qt::CaseInsensitive) == 0)
            return line.mid(colon + 1).trimmed();
    }
    return QByteArray();
}

bool parseContentLength(const QByteArray &requestHead, qint64 *length)
{
    *length = 0;
    const QByteArray value = headerValue(requestHead, "Content-Length");
    if (value.isEmpty())
        return true;

    bool ok = false;
    const qint64 parsed = value.toLongLong(&ok);
    if (!ok || parsed < 0)
        return false;

    *length = parsed;
    return true;
}

bool isJsonContentType(const QByteArray &requestHead)
{
    const QByteArray value = headerValue(requestHead, "Content-Type");
    if (value.isEmpty())
        return true; // 宽松处理：未携带 Content-Type 时仍尝试按 JSON 解析

    return value.split(';').first().trimmed().toLower() == "application/json";
}

// 兼容 Qt 5.12：QJsonObject::value() 只有单参数重载，没有默认值重载。
// 因此统一用 contains() 判断可选字段，再按类型读取；缺失字段保留默认值。
bool readNumber(const QJsonObject &object, const char *key, double *target, QString *error)
{
    const QString name = QString::fromLatin1(key);
    if (!object.contains(name))
        return true;

    const QJsonValue value = object.value(name);
    if (!value.isDouble()) {
        *error = QStringLiteral("Field '%1' must be a number").arg(name);
        return false;
    }

    *target = value.toDouble();
    return true;
}

bool readBool(const QJsonObject &object, const char *key, bool *target, QString *error)
{
    const QString name = QString::fromLatin1(key);
    if (!object.contains(name))
        return true;

    const QJsonValue value = object.value(name);
    if (!value.isBool()) {
        *error = QStringLiteral("Field '%1' must be a boolean").arg(name);
        return false;
    }

    *target = value.toBool();
    return true;
}

bool readString(const QJsonObject &object, const char *key, std::string *target,
                QString *error)
{
    const QString name = QString::fromLatin1(key);
    if (!object.contains(name))
        return true;

    const QJsonValue value = object.value(name);
    if (!value.isString()) {
        *error = QStringLiteral("Field '%1' must be a string").arg(name);
        return false;
    }

    *target = value.toString().toStdString();
    return true;
}

bool readObject(const QJsonObject &root, const char *key, QJsonObject *target, QString *error)
{
    const QString name = QString::fromLatin1(key);
    if (!root.contains(name))
        return true;

    const QJsonValue value = root.value(name);
    if (!value.isObject()) {
        *error = QStringLiteral("Field '%1' must be an object").arg(name);
        return false;
    }

    *target = value.toObject();
    return true;
}

// 显式提供位置对象时要求 x、y 都存在；缺失整个对象则保留当前值。
bool applyPositionJson(const QJsonObject &root, const char *key,
                       Position2D *position, bool *present, QString *error)
{
    const QString name = QString::fromLatin1(key);
    if (!root.contains(name))
        return true;

    QJsonObject object;
    if (!readObject(root, key, &object, error))
        return false;

    if (!object.contains(QStringLiteral("x"))
        || !object.contains(QStringLiteral("y"))) {
        *error = QStringLiteral("Field '%1' requires both x and y").arg(name);
        return false;
    }

    if (!readNumber(object, "x", &position->x, error)
        || !readNumber(object, "y", &position->y, error)) {
        return false;
    }

    if (present)
        *present = true;
    return true;
}

// 配置文件只包含 radar 与 jammer，字段均可选。
bool applyConfigJson(const QJsonObject &root, RadarParams *radar,
                     JammerParams *jammer, QString *error)
{
    QJsonObject radarObject;
    if (!readObject(root, "radar", &radarObject, error)
        || !readNumber(radarObject, "Pt", &radar->Pt, error)
        || !readNumber(radarObject, "Gt", &radar->Gt, error)
        || !readNumber(radarObject, "lambda", &radar->lambda, error)
        || !readNumber(radarObject, "Pmin", &radar->Pmin, error)
        || !readNumber(radarObject, "loss", &radar->loss, error)) {
        return false;
    }

    QJsonObject jammerObject;
    if (!readObject(root, "jammer", &jammerObject, error)
        || !readNumber(jammerObject, "Pj", &jammer->Pj, error)
        || !readNumber(jammerObject, "Gj", &jammer->Gj, error)
        || !readNumber(jammerObject, "js_threshold_db", &jammer->js_threshold_db, error)
        || !readString(jammerObject, "strategy", &jammer->strategy, error)) {
        return false;
    }

    return true;
}

// 所有字段均可选，缺失时保留 input 的当前值。
bool applySimInput(const QJsonObject &root, SimInput *input, QString *error)
{
    if (!applyConfigJson(root, &input->radar, &input->jammer, error))
        return false;

    const bool hasTarget = root.contains(QStringLiteral("target"));
    const bool hasJammerPosition =
        root.contains(QStringLiteral("jammer_position"));

    QJsonObject target;
    if (!readObject(root, "target", &target, error)
        || !readNumber(target, "x", &input->target.x, error)
        || !readNumber(target, "y", &input->target.y, error)
        || !readNumber(target, "rcs", &input->target.rcs, error)) {
        return false;
    }

    if (!applyPositionJson(root, "radar_position", &input->radar_position,
                           nullptr, error)
        || !applyPositionJson(root, "jammer_position", &input->jammer_position,
                              &input->jammer_position_valid, error)) {
        return false;
    }

    // 兼容旧版只更新 target 的客户端：未提供 jammer_position 时维持
    // “干扰机随目标”的自卫干扰几何，而不是沿用上一次的独立位置。
    if (hasTarget && !hasJammerPosition) {
        input->jammer_position.x = input->target.x;
        input->jammer_position.y = input->target.y;
        input->jammer_position_valid = true;
    }

    if (!readBool(root, "esm_on", &input->esm_on, error)
        || !readBool(root, "jammer_on", &input->jammer_on, error)) {
        return false;
    }

    return true;
}

bool parseSimInput(const QByteArray &body, SimInput *input, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("Invalid JSON: %1").arg(parseError.errorString());
        return false;
    }
    if (!document.isObject()) {
        *error = QStringLiteral("Request body must be a JSON object");
        return false;
    }

    return applySimInput(document.object(), input, error);
}

QJsonObject radarToJson(const RadarParams &radar)
{
    QJsonObject object;
    object.insert(QStringLiteral("Pt"), radar.Pt);
    object.insert(QStringLiteral("Gt"), radar.Gt);
    object.insert(QStringLiteral("lambda"), radar.lambda);
    object.insert(QStringLiteral("Pmin"), radar.Pmin);
    object.insert(QStringLiteral("loss"), radar.loss);
    return object;
}

QJsonObject jammerToJson(const JammerParams &jammer)
{
    QJsonObject object;
    object.insert(QStringLiteral("Pj"), jammer.Pj);
    object.insert(QStringLiteral("Gj"), jammer.Gj);
    object.insert(QStringLiteral("js_threshold_db"), jammer.js_threshold_db);
    object.insert(QStringLiteral("strategy"),
                  QString::fromStdString(jammer.strategy));
    return object;
}

QJsonObject configToJson(const RadarParams &radar, const JammerParams &jammer)
{
    QJsonObject root;
    root.insert(QStringLiteral("radar"), radarToJson(radar));
    root.insert(QStringLiteral("jammer"), jammerToJson(jammer));
    return root;
}

QJsonObject positionToJson(const Position2D &position)
{
    QJsonObject object;
    object.insert(QStringLiteral("x"), position.x);
    object.insert(QStringLiteral("y"), position.y);
    return object;
}

QJsonObject sceneToJson(const SimInput &scene)
{
    QJsonObject target;
    target.insert(QStringLiteral("x"), scene.target.x);
    target.insert(QStringLiteral("y"), scene.target.y);
    target.insert(QStringLiteral("rcs"), scene.target.rcs);

    QJsonObject root;
    root.insert(QStringLiteral("radar"), radarToJson(scene.radar));
    root.insert(QStringLiteral("jammer"), jammerToJson(scene.jammer));
    root.insert(QStringLiteral("target"), target);
    root.insert(QStringLiteral("radar_position"),
                positionToJson(scene.radar_position));
    root.insert(QStringLiteral("jammer_position"),
                positionToJson(scene.jammer_position));
    root.insert(QStringLiteral("esm_on"), scene.esm_on);
    root.insert(QStringLiteral("jammer_on"), scene.jammer_on);
    return root;
}

QJsonObject resultToJson(const SimOutput &out)
{
    QJsonObject result;
    result.insert(QStringLiteral("R0"), out.R0);
    result.insert(QStringLiteral("R_eff"), out.R_eff);
    result.insert(QStringLiteral("R_target"), out.R_target);
    result.insert(QStringLiteral("R_jammer"), out.R_jammer);
    result.insert(QStringLiteral("js_ratio_db"), out.js_ratio_db);
    result.insert(QStringLiteral("esm_detected"), out.esm_detected);
    result.insert(QStringLiteral("target_detected"), out.target_detected);
    result.insert(QStringLiteral("status"), QString::fromStdString(out.status));
    return result;
}

QString errorMessageJson(const QString &message)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("error"));
    object.insert(QStringLiteral("error"), message);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace

HttpServer::HttpServer(const QString &configPath, QObject *parent)
    : QTcpServer(parent)
    , m_wsServer(new QWebSocketServer(QStringLiteral("radar_sim"),
                                      QWebSocketServer::NonSecureMode, this))
    , m_broadcastTimer(new QTimer(this))
    , m_configPath(configPath)
{
    connect(m_wsServer, &QWebSocketServer::newConnection,
            this, &HttpServer::onWsNewConnection);
    connect(m_broadcastTimer, &QTimer::timeout, this, &HttpServer::broadcastState);

    loadConfig();

    // 默认把干扰机放在与目标同距、不同方位的点（交换 x/y）。
    // 这样默认 J/S 仍等价于自卫干扰，同时地图上两个标记不会完全重叠。
    m_scene.jammer_position.x = m_scene.target.y;
    m_scene.jammer_position.y = m_scene.target.x;
    m_scene.jammer_position_valid = true;

    m_broadcastTimer->setTimerType(Qt::PreciseTimer);
    m_broadcastTimer->start(kBroadcastIntervalMs);
}

bool HttpServer::listenWebSocket(const QHostAddress &address, quint16 port)
{
    if (!m_wsServer->listen(address, port)) {
        qCritical() << "WebSocket listen on" << port
                    << "failed:" << m_wsServer->errorString();
        return false;
    }

    qInfo().noquote() << QStringLiteral("WebSocket listening on ws://localhost:%1/ws")
                             .arg(m_wsServer->serverPort());
    return true;
}

void HttpServer::loadConfig()
{
    QFile file(m_configPath);
    if (!file.exists()) {
        qInfo().noquote() << QStringLiteral(
            "Config file %1 not found, using built-in defaults").arg(m_configPath);
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << QStringLiteral("Cannot open config file %1: %2")
                                    .arg(m_configPath, file.errorString());
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        const QString reason = parseError.error != QJsonParseError::NoError
            ? parseError.errorString()
            : QStringLiteral("root must be an object");
        qWarning().noquote() << QStringLiteral("Invalid config file %1: %2")
                                    .arg(m_configPath, reason);
        return;
    }

    SimConfig loaded = m_config;
    QString error;
    if (!applyConfigJson(document.object(), &loaded.radar, &loaded.jammer, &error)) {
        qWarning().noquote() << QStringLiteral("Invalid config file %1: %2")
                                    .arg(m_configPath, error);
        return;
    }

    m_config = loaded;
    m_scene.radar = loaded.radar;
    m_scene.jammer = loaded.jammer;

    qInfo().noquote() << QStringLiteral("Loaded config from %1 (Gt=%2, Gj=%3)")
                             .arg(m_configPath)
                             .arg(m_config.radar.Gt)
                             .arg(m_config.jammer.Gj);
}

bool HttpServer::writeConfigFile(const SimConfig &config, QString *error) const
{
    QSaveFile file(m_configPath);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }

    const QByteArray payload = QJsonDocument(configToJson(config.radar, config.jammer))
                                   .toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        *error = file.errorString();
        file.cancelWriting();
        return false;
    }

    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }

    return true;
}

void HttpServer::incomingConnection(qintptr socketDescriptor)
{
    auto *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        qWarning() << "HttpServer: setSocketDescriptor failed:" << socket->errorString();
        delete socket;
        return;
    }

    m_buffers.insert(socket, QByteArray());
    connect(socket, &QTcpSocket::readyRead, this, &HttpServer::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &HttpServer::onSocketDisconnected);
}

void HttpServer::onReadyRead()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;

    auto it = m_buffers.find(socket);
    if (it == m_buffers.end())
        return;

    QByteArray &buffer = *it;
    buffer.append(socket->readAll());

    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (buffer.size() > kMaxHeaderBytes)
            sendResponse(socket, 431, "text/plain", "Request header too large\n");
        return;
    }
    if (headerEnd > kMaxHeaderBytes) {
        sendResponse(socket, 431, "text/plain", "Request header too large\n");
        return;
    }

    const QByteArray requestHead = buffer.left(headerEnd + 4);

    qint64 contentLength = 0;
    if (!parseContentLength(requestHead, &contentLength)) {
        sendResponse(socket, 400, "text/plain", "Bad Content-Length\n");
        return;
    }
    if (contentLength > kMaxBodyBytes) {
        sendResponse(socket, 413, "text/plain", "Request body too large\n");
        return;
    }

    if (buffer.size() < headerEnd + 4 + contentLength)
        return; // 请求体尚未接收完整

    const QByteArray body = buffer.mid(headerEnd + 4, static_cast<int>(contentLength));
    buffer.clear();
    handleRequest(socket, requestHead, body);
}

void HttpServer::onSocketDisconnected()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;

    m_buffers.remove(socket);
    socket->deleteLater();
}

void HttpServer::handleRequest(QTcpSocket *socket, const QByteArray &requestHead,
                               const QByteArray &body)
{
    const QList<QByteArray> lines = requestHead.split('\n');
    const QList<QByteArray> parts = lines.value(0).trimmed().split(' ');
    if (parts.size() < 2) {
        sendResponse(socket, 400, "text/plain", "Bad Request\n");
        return;
    }

    const QByteArray method = parts.at(0);
    QByteArray target = parts.at(1);
    const int queryPos = target.indexOf('?');
    if (queryPos >= 0)
        target.truncate(queryPos);

    const ScopedRequestLog requestLog(method, target);
    qDebug().noquote() << QStringLiteral("HTTP request body: %1 bytes").arg(body.size());

    if (target == QByteArrayLiteral("/health")) {
        if (method == QByteArrayLiteral("GET")) {
            QJsonObject health;
            health.insert(QStringLiteral("ok"), true);
            sendResponse(socket, 200, "application/json",
                         QJsonDocument(health).toJson(QJsonDocument::Compact));
        } else {
            sendResponse(socket, 405, "text/plain", "Method Not Allowed\n");
        }
        return;
    }

    if (target == QByteArrayLiteral("/api/simulate")) {
        if (method == QByteArrayLiteral("POST"))
            handleSimulate(socket, requestHead, body);
        else if (method == QByteArrayLiteral("OPTIONS"))
            sendResponse(socket, 204, QByteArray(), QByteArray());
        else
            sendResponse(socket, 405, "text/plain", "Method Not Allowed\n");
        return;
    }

    if (target == QByteArrayLiteral("/api/config")) {
        if (method == QByteArrayLiteral("GET") || method == QByteArrayLiteral("POST"))
            handleConfig(socket, requestHead, body, method);
        else if (method == QByteArrayLiteral("OPTIONS"))
            sendResponse(socket, 204, QByteArray(), QByteArray());
        else
            sendResponse(socket, 405, "text/plain", "Method Not Allowed\n");
        return;
    }

    sendJsonError(socket, 404, QStringLiteral("not found"));
}

void HttpServer::handleSimulate(QTcpSocket *socket, const QByteArray &requestHead,
                                const QByteArray &body)
{
    if (!isJsonContentType(requestHead)) {
        sendJsonError(socket, 415, QStringLiteral("Content-Type must be application/json"));
        return;
    }

    // 缺省雷达/干扰参数来自当前配置。
    SimInput input;
    {
        QMutexLocker locker(&m_configMutex);
        input.radar = m_config.radar;
        input.jammer = m_config.jammer;
    }

    QString error;
    if (!parseSimInput(body, &input, &error)) {
        sendJsonError(socket, 400, error);
        return;
    }

    qDebug().noquote() << QStringLiteral("simulate scene: %1")
                              .arg(QString::fromUtf8(
                                  QJsonDocument(sceneToJson(input)).toJson(QJsonDocument::Compact)));

    const DetectionEngine engine;
    const SimOutput out = engine.evaluateScene(input);

    const QByteArray payload = QJsonDocument(resultToJson(out)).toJson(QJsonDocument::Compact);
    qDebug().noquote() << QStringLiteral("simulate result: %1")
                              .arg(QString::fromUtf8(payload));
    sendResponse(socket, 200, "application/json", payload);
}

void HttpServer::handleConfig(QTcpSocket *socket, const QByteArray &requestHead,
                              const QByteArray &body, const QByteArray &method)
{
    if (method == QByteArrayLiteral("GET")) {
        QJsonObject configObject;
        {
            QMutexLocker locker(&m_configMutex);
            configObject = configToJson(m_config.radar, m_config.jammer);
        }

        sendResponse(socket, 200, "application/json",
                     QJsonDocument(configObject).toJson(QJsonDocument::Compact));
        return;
    }

    // POST /api/config
    if (!isJsonContentType(requestHead)) {
        sendJsonError(socket, 415, QStringLiteral("Content-Type must be application/json"));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        sendJsonError(socket, 400,
                      QStringLiteral("Invalid JSON: %1").arg(parseError.errorString()));
        return;
    }
    if (!document.isObject()) {
        sendJsonError(socket, 400, QStringLiteral("Request body must be a JSON object"));
        return;
    }

    SimConfig updated;
    {
        QMutexLocker locker(&m_configMutex);
        updated = m_config;
    }

    QString error;
    if (!applyConfigJson(document.object(), &updated.radar, &updated.jammer, &error)) {
        sendJsonError(socket, 400, error);
        return;
    }

    if (!writeConfigFile(updated, &error)) {
        sendJsonError(socket, 500,
                      QStringLiteral("Failed to write config file: %1").arg(error));
        return;
    }

    {
        QMutexLocker locker(&m_configMutex);
        m_config = updated;
    }
    {
        QMutexLocker locker(&m_sceneMutex);
        m_scene.radar = updated.radar;
        m_scene.jammer = updated.jammer;
    }

    qInfo().noquote() << QStringLiteral("Config updated via API and saved to %1")
                             .arg(m_configPath);

    broadcastState();

    const QByteArray payload = QJsonDocument(configToJson(updated.radar, updated.jammer))
                                   .toJson(QJsonDocument::Compact);
    sendResponse(socket, 200, "application/json", payload);
}

void HttpServer::sendJsonError(QTcpSocket *socket, int statusCode, const QString &message)
{
    QJsonObject errorObject;
    errorObject.insert(QStringLiteral("error"), message);

    sendResponse(socket, statusCode, "application/json",
                 QJsonDocument(errorObject).toJson(QJsonDocument::Compact));
}

void HttpServer::sendResponse(QTcpSocket *socket, int statusCode,
                              const QByteArray &contentType, const QByteArray &body)
{
    if (!socket)
        return;

    const QString httpDate = QLocale::c().toString(
        QDateTime::currentDateTimeUtc(), QStringLiteral("ddd, dd MMM yyyy HH:mm:ss 'GMT'"));

    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(statusCode) + ' '
              + statusText(statusCode) + "\r\n";
    response += "Date: " + httpDate.toLatin1() + "\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n";
    response += "Access-Control-Allow-Headers: Content-Type\r\n";
    response += "Cache-Control: no-store\r\n";
    response += "Connection: close\r\n";

    if (statusCode != 204) {
        response += "Content-Type: " + contentType + "\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        response += "\r\n";
        response += body;
    } else {
        response += "\r\n";
    }

    m_buffers.remove(socket);
    socket->write(response);
    socket->disconnectFromHost();
}

void HttpServer::onWsNewConnection()
{
    while (QWebSocket *socket = m_wsServer->nextPendingConnection()) {
        if (socket->requestUrl().path() != QLatin1String("/ws")) {
            qWarning() << "WebSocket rejected path:" << socket->requestUrl().toString();
            socket->close(QWebSocketProtocol::CloseCodeProtocolError,
                          QStringLiteral("Invalid path"));
            socket->deleteLater();
            continue;
        }

        m_clients.append(socket);
        connect(socket, &QWebSocket::textMessageReceived,
                this, &HttpServer::onWsTextMessage);
        connect(socket, &QWebSocket::disconnected,
                this, &HttpServer::onWsDisconnected);

        qInfo() << "WebSocket client connected:"
                << socket->peerAddress().toString() << socket->peerPort();
    }
}

void HttpServer::onWsTextMessage(const QString &message)
{
    auto *socket = qobject_cast<QWebSocket *>(sender());
    if (!socket)
        return;

    qDebug().noquote() << QStringLiteral("WebSocket message: %1").arg(message);

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        socket->sendTextMessage(errorMessageJson(QStringLiteral("Invalid JSON message")));
        return;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("type")).toString() != QLatin1String("update")) {
        socket->sendTextMessage(errorMessageJson(QStringLiteral("Unsupported message type")));
        return;
    }

    const QJsonValue sceneValue = root.value(QStringLiteral("scene"));
    if (!sceneValue.isObject()) {
        socket->sendTextMessage(
            errorMessageJson(QStringLiteral("Field 'scene' must be an object")));
        return;
    }

    QString error;
    bool ok = false;
    {
        QMutexLocker locker(&m_sceneMutex);
        SimInput updated = m_scene; // 在现有场景基础上增量更新
        ok = applySimInput(sceneValue.toObject(), &updated, &error);
        if (ok)
            m_scene = updated;
    }

    if (!ok) {
        socket->sendTextMessage(errorMessageJson(error));
        return;
    }

    broadcastState();
}

void HttpServer::onWsDisconnected()
{
    auto *socket = qobject_cast<QWebSocket *>(sender());
    if (!socket)
        return;

    m_clients.removeAll(socket);
    socket->deleteLater();
}

void HttpServer::broadcastState()
{
    if (m_clients.isEmpty())
        return; // 无客户端时跳过发送，定时器继续运行

    const QString message = stateMessage();
    for (QWebSocket *client : qAsConst(m_clients))
        client->sendTextMessage(message);
}

QString HttpServer::stateMessage() const
{
    SimInput scene;
    {
        QMutexLocker locker(&m_sceneMutex);
        scene = m_scene;
    }

    const DetectionEngine engine;
    const SimOutput result = engine.evaluateScene(scene);

    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("state"));
    message.insert(QStringLiteral("scene"), sceneToJson(scene));
    message.insert(QStringLiteral("result"), resultToJson(result));
    return QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
}
