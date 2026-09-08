#pragma once

#include <QMap>
#include <QMetaType>
#include <QString>
#include <QStringList>

/*
 * PA/ARM 串口协议定义。
 *
 * 当前 ARM 端 pa_controller 同时保留两类入口：
 *   - 上位机发送一行命令，结尾为 \r\n 或 \n。
 *   - ARM 返回一行响应，成功以 OK 开头，失败以 ERR 开头。
 *   - 正式二进制协议由 PaBinaryProtocol 负责，不能在这里拼接文本命令。
 *
 * 这个类只负责“命令字符串”和“响应文本解析”，不负责串口读写。
 * ASCII 命令仅用于研发调试和兼容；正式上位机应使用 PaBinaryProtocol。
 */
class PaProtocol {
public:
    enum class Command {
        Ping,
        Status,
        LoadTemplate,
        MakeOffset,
        MakeGain,
        ConfigTemplate,
        StartCorrection,
        SendSingle,
        StartContinuous,
        StopTransfer,
        StopDynamic
    };

    struct Response {
        bool ok = false;             // true 表示响应以 OK 开头
        bool error = false;          // true 表示响应以 ERR 开头
        QString keyword;             // OK/ERR 后面的第一个字段，例如 STATUS、PONG、UNKNOWN
        QString rawLine;             // 原始响应行，保留给日志窗口显示
        QMap<QString, QString> kv;   // key=value 字段，例如 wr_state=1
    };

    static QString commandText(Command command);
    static QString commandName(Command command);
    static QStringList commandNames();
    static Response parseResponse(const QString& line);
};

Q_DECLARE_METATYPE(PaProtocol::Command)
