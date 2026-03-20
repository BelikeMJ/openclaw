#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QWebSocket>
#include <QScrollBar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>

class OpenClawChat : public QWidget {
    Q_OBJECT
public:
    OpenClawChat(QWidget *parent = nullptr) : QWidget(parent) {
        setWindowTitle("OpenClaw Chat");
        resize(600, 500);

        // UI
        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        
        // 标题
        QLabel *title = new QLabel("🤖 OpenClaw Chat", this);
        title->setStyleSheet("font-size: 18px; font-weight: bold; padding: 10px;");
        mainLayout->addWidget(title);

        // 连接状态
        statusLabel = new QLabel("状态: 连接中...", this);
        statusLabel->setStyleSheet("padding: 5px; color: #ffb74d;");
        mainLayout->addWidget(statusLabel);

        // 显示区域
        display = new QTextEdit(this);
        display->setReadOnly(true);
        display->setStyleSheet("background-color: #1e1e1e; color: #ddd; font-size: 14px; padding: 10px;");
        mainLayout->addWidget(display);

        // 输入区域
        QHBoxLayout *inputLayout = new QHBoxLayout();
        input = new QLineEdit(this);
        input->setPlaceholderText("输入指令...");
        input->setStyleSheet("padding: 10px; font-size: 14px;");
        inputLayout->addWidget(input);

        sendBtn = new QPushButton("发送", this);
        sendBtn->setStyleSheet("padding: 10px 20px; font-size: 14px; background-color: #0078d4; color: white; border: none;");
        sendBtn->setEnabled(false);
        inputLayout->addWidget(sendBtn);

        mainLayout->addLayout(inputLayout);

        // WebSocket
        socket = new QWebSocket(QString(), QWebSocketProtocol::Version13, this);
        
        // 信号槽
        connect(sendBtn, &QPushButton::clicked, this, &OpenClawChat::sendMessage);
        connect(input, &QLineEdit::returnPressed, this, &OpenClawChat::sendMessage);
        connect(socket, &QWebSocket::connected, this, &OpenClawChat::onConnected);
        connect(socket, &QWebSocket::disconnected, this, &OpenClawChat::onDisconnected);
        connect(socket, &QWebSocket::textMessageReceived, this, &OpenClawChat::onMessage);
        connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this, &OpenClawChat::onError);

        // 连接
        appendMessage("system", "正在连接 ws://127.0.0.1:18789 ...");
        socket->open(QUrl("ws://127.0.0.1:18789"));
        
        pendingId = "";
        connected = false;
    }

private slots:
    void sendMessage() {
        QString msg = input->text().trimmed();
        if (msg.isEmpty() || socket->state() != QAbstractSocket::ConnectedState || !connected) return;

        // 显示用户消息
        appendMessage("user", msg);
        input->clear();

        // 禁用发送
        sendBtn->setEnabled(false);

        // 发送聊天消息
        QJsonObject json;
        json["type"] = "req";
        json["id"] = QUuid::createUuid().toString();
        json["method"] = "chat.send";
        json["params"] = QJsonObject({
            {"sessionKey", "main"},
            {"message", msg}
        });

        pendingId = json["id"].toString();
        socket->sendTextMessage(QJsonDocument(json).toJson());
    }

    void onConnected() {
        appendMessage("system", "WebSocket 已连接，等待认证...");
    }

    void onDisconnected() {
        statusLabel->setText("状态: 断开连接");
        statusLabel->setStyleSheet("padding: 5px; color: #f44336;");
        sendBtn->setEnabled(false);
        connected = false;
        appendMessage("system", "连接断开");
    }

    void onMessage(QString message) {
        QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
        if (!doc.isObject()) return;
        
        QJsonObject obj = doc.object();
        QString type = obj["type"].toString();
        
        // 连接挑战 - 需要响应
        if (type == "event" && obj["event"].toString() == "connect.challenge") {
            QJsonObject payload = obj["payload"].toObject();
            QString nonce = payload["nonce"].toString();
            qint64 ts = payload["ts"].toDouble();
            
            // 发送 connect 请求
            QJsonObject json;
            json["type"] = "req";
            json["id"] = QUuid::createUuid().toString();
            json["method"] = "connect";
            json["params"] = QJsonObject({
                {"minProtocol", 3},
                {"maxProtocol", 3},
                {"client", QJsonObject({
                    {"id", "openclaw-control-ui"},
                    {"version", "1.0.0"},
                    {"platform", "linux"},
                    {"mode", "webchat"}
                })},
                {"role", "operator"},
                {"scopes", QJsonArray({"operator.admin"})},
                {"caps", QJsonArray({"tool-events"})}
            });
            
            pendingConnectId = json["id"].toString();
            socket->sendTextMessage(QJsonDocument(json).toJson());
            appendMessage("system", "发送认证请求...");
            return;
        }
        
        // 检查是否是响应
        if (type == "res") {
            QString id = obj["id"].toString();
            
            if (obj["ok"].toBool()) {
                // 检查是否是 connect 响应
                if (id == pendingConnectId) {
                    connected = true;
                    statusLabel->setText("状态: 已连接");
                    statusLabel->setStyleSheet("padding: 5px; color: #81c784;");
                    sendBtn->setEnabled(true);
                    appendMessage("system", "✓ 连接成功！可以聊天了。");
                    pendingConnectId = "";
                } else if (id == pendingId) {
                    sendBtn->setEnabled(true);
                    pendingId = "";
                }
            } else {
                QString error = obj["error"].toObject()["message"].toString();
                appendMessage("system", "错误: " + error);
                sendBtn->setEnabled(true);
            }
            return;
        }
        
        // 检查是否是事件
        if (type == "event") {
            QString event = obj["event"].toString();
            if (event == "chat") {
                QJsonObject payload = obj["payload"].toObject();
                QString state = payload["state"].toString();
                
                if (state == "delta") {
                    QJsonObject msg = payload["message"].toObject();
                    QString text = msg["text"].toString();
                    if (!text.isEmpty()) {
                        appendMessage("assistant", text);
                    }
                } else if (state == "final") {
                    QJsonObject msg = payload["message"].toObject();
                    QString role = msg["role"].toString();
                    if (role == "assistant") {
                        QJsonArray content = msg["content"].toArray();
                        for (int i = 0; i < content.size(); i++) {
                            QJsonObject item = content[i].toObject();
                            if (item["type"].toString() == "text") {
                                QString text = item["text"].toString();
                                if (!text.isEmpty()) {
                                    appendMessage("assistant", text);
                                }
                            }
                        }
                    }
                }
            }
            return;
        }
        
        // hello 响应
        if (obj.contains("snapshot")) {
            appendMessage("system", "✓ Gateway 已连接");
        }
    }

    void onError(QAbstractSocket::SocketError error) {
        appendMessage("system", "错误: " + QString::number(error));
    }

    void appendMessage(const QString &type, const QString &msg) {
        QString color;
        QString prefix;
        
        if (type == "user") {
            color = "#4fc3f7";
            prefix = "你";
        } else if (type == "assistant") {
            color = "#81c784";
            prefix = "🤖";
        } else {
            color = "#ffb74d";
            prefix = "系统";
        }
        
        display->append(QString("<div style='margin: 5px 0;'><span style='color: %1; font-weight: bold;'>%2: </span><span style='color: #ddd;'>%3</span></div>")
            .arg(color).arg(prefix).arg(msg.toHtmlEscaped()));
        
        // 滚动到底部
        QScrollBar *sb = display->verticalScrollBar();
        sb->setValue(sb->maximum());
    }

private:
    QTextEdit *display;
    QLineEdit *input;
    QPushButton *sendBtn;
    QLabel *statusLabel;
    QWebSocket *socket;
    QString pendingId;
    QString pendingConnectId;
    bool connected;
};

#include "main.moc"

int main(int argc, char *argv[]) {
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv);
    OpenClawChat window;
    window.show();
    return app.exec();
}
