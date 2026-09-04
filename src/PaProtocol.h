#pragma once

#include <QMap>
#include <QMetaType>
#include <QString>
#include <QStringList>

/*
 * PA/ARM 串口协议定义。
 *
 * 当前 ARM 端 pa_controller 使用 ASCII 行协议：
 *   - 上位机发送一行命令，结尾为 \r\n 或 \n。
 *   - ARM 返回一行响应，成功以 OK 开头，失败以 ERR 开头。
 *
 * 这个类只负责“命令字符串”和“响应文本解析”，不负责串口读写。
 * 后续如果协议改成二进制帧，只需要重点替换本类和 SerialClient 的收发边界。
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
