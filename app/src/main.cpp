#include "app_edition.h"
#include "main_window.h"
#include "service_container.h"

#include <QApplication>
#include <QCoreApplication>
#include <QMessageBox>
#include <cstdio>
#include <cstring>

int main(int argc, char *argv[])
{
    for (int index = 1; index < argc; ++index)
    {
        if (std::strcmp(argv[index], "--check-config") != 0)
            continue;
        QCoreApplication app(argc, argv);
        ServiceContainer services;
        QString error;
        if (!services.loadConfig(&error))
        {
            std::fprintf(stderr, "%s\n", error.toLocal8Bit().constData());
            return 1;
        }
        std::puts("Configuration OK");
        return 0;
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(AppEdition::applicationId());
    QApplication::setApplicationDisplayName(AppEdition::displayName());
    QApplication::setOrganizationName(QStringLiteral("Burner"));

    ServiceContainer services;
    QString error;
    if (!services.loadConfig(&error))
    {
        QMessageBox::warning(nullptr, QStringLiteral("Configuration warning"),
                             QStringLiteral("Configuration was not fully loaded:\n%1").arg(error));
    }

    MainWindow window(&services);
    window.show();
    return app.exec();
}
