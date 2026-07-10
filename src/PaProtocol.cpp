#include "PaProtocol.h"

#include <QStringList>
#include <QtGlobal>

QString PaProtocol::commandText(Command command) {
    switch (command) {
    case Command::Ping:
        return "PING";
    case Command::Status:
        return "STATUS";
    case Command::WaitIrq:
        return "WAIT_IRQ";
    case Command::LoadTemplate:
        return "LOAD_TEMPLATE";
    case Command::MakeOffset:
        return "MAKE_OFFSET";
    case Command::MakeGain:
        return "MAKE_GAIN";
    case Command::ConfigTemplate:
        return "CONFIG_TEMPLATE";
    case Command::StartCorrection:
        return "START_CORR";
    case Command::SendImage:
        return "SEND_IMAGE";
    case Command::Quit:
        return "QUIT";
    }
    return QString();
}

QString PaProtocol::commandName(Command command) {
    switch (command) {
    case Command::Ping:
        return QStringLiteral("心跳");
    case Command::Status:
        return QStringLiteral("读取状态");
    case Command::WaitIrq:
        return QStringLiteral("等待中断");
    case Command::LoadTemplate:
        return QStringLiteral("加载模板");
    case Command::MakeOffset:
        return QStringLiteral("生成 Offset");
    case Command::MakeGain:
        return QStringLiteral("生成 Gain");
    case Command::ConfigTemplate:
        return QStringLiteral("配置模板");
    case Command::StartCorrection:
        return QStringLiteral("启动校正");
    case Command::SendImage:
        return QStringLiteral("手动上图");
    case Command::Quit:
        return QStringLiteral("退出 ARM");
    }
    return QStringLiteral("未知命令");
}

QStringList PaProtocol::commandNames() {
    return {
        commandText(Command::Ping),
        commandText(Command::Status),
        commandText(Command::WaitIrq),
        commandText(Command::LoadTemplate),
        commandText(Command::MakeOffset),
        commandText(Command::MakeGain),
        commandText(Command::ConfigTemplate),
        commandText(Command::StartCorrection),
        commandText(Command::SendImage),
        commandText(Command::Quit),
    };
}

PaProtocol::Response PaProtocol::parseResponse(const QString& line) {
    Response response;
    response.rawLine = line.trimmed();

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QStringList fields = response.rawLine.split(' ', Qt::SkipEmptyParts);
#else
    const QStringList fields = response.rawLine.split(' ', QString::SkipEmptyParts);
#endif
    if (fields.isEmpty()) {
        return response;
    }

    response.ok = fields.first() == "OK";
    response.error = fields.first() == "ERR";

    if (fields.size() >= 2) {
        response.keyword = fields.at(1);
    }

    /*
     * ARM STATUS 响应形如：
     * OK STATUS int=0x... pa=0x... wr_state=...
     *
     * 这里按 key=value 泛化解析，避免 UI 依赖固定字段顺序。
     */
    for (const QString& field : fields) {
        const int pos = field.indexOf('=');
        if (pos <= 0) {
            continue;
        }
        const QString key = field.left(pos);
        const QString value = field.mid(pos + 1);
        response.kv.insert(key, value);
    }

    return response;
}
