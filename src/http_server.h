#pragma once

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QMutex>
#include <QString>
#include <QTcpServer>

#include "types.h"

class QTcpSocket;
class QTimer;
class QWebSocket;
class QWebSocketServer;

// 基于 QTcpServer 的最小 HTTP/1.1 服务，同时持有 WebSocket 服务。
// HTTP 路由：GET /health、POST /api/simulate、GET/POST /api/config、OPTIONS
// WS 端点：ws://<host>:<port>/ws（默认 8081）
class HttpServer : public QTcpServer {
    Q_OBJECT

public:
    explicit HttpServer(const QString &configPath = QStringLiteral("config.json"),
                        QObject *parent = nullptr);

    bool listenWebSocket(const QHostAddress &address = QHostAddress::Any,
                         quint16 port = 8081);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private slots:
    void onReadyRead();
    void onSocketDisconnected();
    void onWsNewConnection();
    void onWsTextMessage(const QString &message);
    void onWsDisconnected();
    void broadcastState();

private:
    void handleRequest(QTcpSocket *socket, const QByteArray &requestHead,
                       const QByteArray &body);
    void handleSimulate(QTcpSocket *socket, const QByteArray &requestHead,
                        const QByteArray &body);
    void handleConfig(QTcpSocket *socket, const QByteArray &requestHead,
                      const QByteArray &body, const QByteArray &method);
    void sendResponse(QTcpSocket *socket, int statusCode,
                      const QByteArray &contentType, const QByteArray &body);
    void sendJsonError(QTcpSocket *socket, int statusCode, const QString &message);
    QString stateMessage() const;

    void loadConfig();
    bool writeConfigFile(const SimConfig &config, QString *error) const;

    QHash<QTcpSocket *, QByteArray> m_buffers;

    QWebSocketServer *m_wsServer = nullptr;
    QTimer *m_broadcastTimer = nullptr;
    QList<QWebSocket *> m_clients;

    SimInput m_scene;
    mutable QMutex m_sceneMutex;

    QString m_configPath;
    SimConfig m_config;
    mutable QMutex m_configMutex;
};
