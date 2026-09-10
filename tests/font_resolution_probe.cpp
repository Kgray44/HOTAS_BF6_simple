#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QQuickView>
#include <QQuickStyle>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QScreen>
#include <QTextStream>
#include <QTimer>
#include <QVariant>
#include <QtGlobal>

namespace {

constexpr auto kDisplayFont = "Segoe UI";
constexpr auto kTechnicalFont = "Consolas";
constexpr auto kHistoricalVariableRequest = "Segoe UI Variable";

struct FontResolution {
    QString requested;
    QFont requestedFont;
    QFontInfo resolved;
};

FontResolution resolve(const QString &family, int pixelSize)
{
    QFont requested(family);
    requested.setPixelSize(pixelSize);
    return {family, requested, QFontInfo(requested)};
}

void writeResolution(QTextStream &stream, const QString &role, const FontResolution &resolution)
{
    stream << role << ": requested='" << resolution.requested << "'"
           << " resolved='" << resolution.resolved.family() << "'"
           << " style='" << resolution.resolved.styleName() << "'"
           << " weight=" << resolution.resolved.weight()
           << " requestedPixelSize=" << resolution.requestedFont.pixelSize()
           << " requestedPointSize=" << resolution.requestedFont.pointSizeF()
           << " resolvedPixelSize=" << resolution.resolved.pixelSize()
           << " resolvedPointSize=" << resolution.resolved.pointSizeF() << Qt::endl;
}

bool reportQmlFont(QTextStream &stream, QObject *root, const char *objectName,
                   const QString &expectedFamily, bool verify)
{
    QObject *object = root ? root->findChild<QObject *>(QString::fromLatin1(objectName)) : nullptr;
    if (!object) {
        stream << objectName << ": QML object was not created" << Qt::endl;
        return false;
    }
    const QVariant fontValue = object->property("font");
    if (!fontValue.canConvert<QFont>()) {
        stream << objectName << ": QML font property is unavailable" << Qt::endl;
        return false;
    }
    const QFont font = qvariant_cast<QFont>(fontValue);
    const QFontInfo info(font);
    writeResolution(stream, QString::fromLatin1(objectName),
                    {expectedFamily, font, info});
    return !verify || info.family().compare(expectedFamily, Qt::CaseInsensitive) == 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication application(argc, argv);
    application.setFont(QFont(QString::fromLatin1(kDisplayFont)));
    const bool verify = application.arguments().contains(QStringLiteral("--verify-flight-deck-contract"));
    const int captureArgument = application.arguments().indexOf(QStringLiteral("--capture"));
    const QString capturePath = captureArgument >= 0 && captureArgument + 1 < application.arguments().size()
        ? application.arguments().at(captureArgument + 1) : QString{};
    QTextStream stream(stdout);

    const QStringList families = QFontDatabase::families();
    const bool hasDisplayFont = families.contains(QString::fromLatin1(kDisplayFont), Qt::CaseInsensitive);
    const bool hasVariableUmbrella = families.contains(QString::fromLatin1(kHistoricalVariableRequest), Qt::CaseInsensitive);
    const bool hasTechnicalFont = families.contains(QString::fromLatin1(kTechnicalFont), Qt::CaseInsensitive);

    stream << "qt=" << qVersion()
           << " platform=" << QGuiApplication::platformName()
           << " devicePixelRatio=" << (QGuiApplication::primaryScreen()
                   ? QGuiApplication::primaryScreen()->devicePixelRatio() : 0.0)
           << " displayFontInstalled=" << hasDisplayFont
           << " variableUmbrellaInstalled=" << hasVariableUmbrella
           << " technicalFontInstalled=" << hasTechnicalFont << Qt::endl;

    const QFont applicationFont = QApplication::font();
    writeResolution(stream, QStringLiteral("QApplication.default"),
                    {applicationFont.family(), applicationFont, QFontInfo(applicationFont)});
    writeResolution(stream, QStringLiteral("requested.variableUmbrella"),
                    resolve(QString::fromLatin1(kHistoricalVariableRequest), 16));
    const FontResolution display = resolve(QString::fromLatin1(kDisplayFont), 16);
    const FontResolution technical = resolve(QString::fromLatin1(kTechnicalFont), 16);
    writeResolution(stream, QStringLiteral("requested.display"), display);
    writeResolution(stream, QStringLiteral("requested.technical"), technical);

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick 6.5
        import QtQuick.Controls 6.5
        Rectangle {
            width: 1280
            height: 720
            color: "#132331"
            Text {
                objectName: "qtQuickText"
                x: 48
                y: 48
                text: "Flight Deck display"
                color: "#eef7fb"
                font.family: "Segoe UI"
                font.pixelSize: 16
            }
            Label {
                objectName: "qtQuickControlsLabel"
                x: 48
                y: 96
                text: "Flight Deck display"
                color: "#eef7fb"
                font.family: "Segoe UI"
                font.pixelSize: 16
            }
            Text {
                objectName: "qtQuickTechnicalText"
                x: 48
                y: 144
                text: "X / Y / Z"
                color: "#eef7fb"
                font.family: "Consolas"
                font.pixelSize: 16
            }
            Text {
                objectName: "qtQuickDefaultText"
                x: 48
                y: 192
                text: "Flight Deck inherited body"
                color: "#eef7fb"
                font.pixelSize: 16
            }
            Text {
                objectName: "qtQuickVariableText"
                x: 48
                y: 240
                text: "Segoe UI Variable / Overview / ACTIVE DEVICE RIG"
                color: "#eef7fb"
                font.family: "Segoe UI Variable"
                font.pixelSize: 30
            }
            Text {
                objectName: "qtQuickStableText"
                x: 48
                y: 320
                text: "Segoe UI / Overview / ACTIVE DEVICE RIG"
                color: "#eef7fb"
                font.family: "Segoe UI"
                font.pixelSize: 30
            }
        }
    )", QUrl(QStringLiteral("qrc:/font-resolution-probe.qml")));
    QObject *root = component.create();
    if (!root) {
        for (const QQmlError &error : component.errors()) stream << error.toString() << Qt::endl;
        return 1;
    }

    bool qmlContract = true;
    qmlContract &= reportQmlFont(stream, root, "qtQuickText", QString::fromLatin1(kDisplayFont), verify);
    qmlContract &= reportQmlFont(stream, root, "qtQuickControlsLabel", QString::fromLatin1(kDisplayFont), verify);
    qmlContract &= reportQmlFont(stream, root, "qtQuickTechnicalText", QString::fromLatin1(kTechnicalFont), verify);
    qmlContract &= reportQmlFont(stream, root, "qtQuickDefaultText", QString::fromLatin1(kDisplayFont), verify);
    qmlContract &= reportQmlFont(stream, root, "qtQuickVariableText",
                                 QString::fromLatin1(kHistoricalVariableRequest), false);

    bool captureSucceeded = true;
    if (!capturePath.isEmpty()) {
        QQuickView view(&engine, nullptr);
        view.setResizeMode(QQuickView::SizeViewToRootObject);
        view.setPosition(-2000, -2000);
        view.setContent(component.url(), &component, root);
        view.show();
        QEventLoop settle;
        QTimer::singleShot(250, &settle, &QEventLoop::quit);
        settle.exec();
        const QFileInfo captureFile(capturePath);
        captureSucceeded = QDir().mkpath(captureFile.absolutePath())
            && view.grabWindow().save(capturePath);
        view.hide();
        stream << "nativeCapture='" << capturePath << "' saved=" << captureSucceeded << Qt::endl;
    } else {
        delete root;
    }

    if (!verify) return captureSucceeded ? 0 : 1;
    const bool contract = hasDisplayFont && hasTechnicalFont
        && display.resolved.family().compare(QString::fromLatin1(kDisplayFont), Qt::CaseInsensitive) == 0
        && technical.resolved.family().compare(QString::fromLatin1(kTechnicalFont), Qt::CaseInsensitive) == 0
        && qmlContract && captureSucceeded;
    if (!contract) {
        stream << "Flight Deck font contract failed: expected resolved display='Segoe UI' and technical='Consolas'." << Qt::endl;
        return 1;
    }
    stream << "Flight Deck font contract passed." << Qt::endl;
    return 0;
}
