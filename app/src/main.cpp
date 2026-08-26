#include "app_edition.h"
#include "main_window.h"
#include "service_container.h"

#include <QApplication>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(AppEdition::displayName());
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
