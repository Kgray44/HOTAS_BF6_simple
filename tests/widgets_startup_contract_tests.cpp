#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QSystemTrayIcon>

#include <memory>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    auto menu = std::make_unique<QMenu>();
    QAction *open = menu->addAction(QStringLiteral("Open HOTAS BF6"));
    if (!open || !qobject_cast<QApplication *>(QCoreApplication::instance())) return 1;

    QSystemTrayIcon tray;
    tray.setContextMenu(menu.get());
    return tray.contextMenu() == menu.get() ? 0 : 1;
}
