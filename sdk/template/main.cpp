// FinnOS Native App Starter Template (C++20 + Qt 6 FinnUIQml).
// Automatically adapts across Desktop, Tablet, Mobile, and TV form factors.

#include <FinnSDK/FinnQml.hpp>
#include <QObject>
#include <QUrl>
#include <cstdio>

class CounterModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit CounterModel(QObject* parent = nullptr) : QObject(parent) {}
    int count() const { return count_; }

    Q_INVOKABLE void increment() { ++count_; emit countChanged(); }
    Q_INVOKABLE void reset() { count_ = 0; emit countChanged(); }

signals:
    void countChanged();

private:
    int count_ = 0;
};

int main(int argc, char** argv) {
    finn::ui::qml::FinnQmlApp app(argc, argv,
                                  QStringLiteral("finn-counter"),
                                  QStringLiteral("Finn Counter"),
                                  QStringLiteral("1.0.0"));

    auto* model = new CounterModel(&app.engine());
    app.setContextProperty(QStringLiteral("Counter"), model);

    if (!app.load(QUrl(QStringLiteral("qrc:/qt/qml/FinnCounter/App.qml")))) {
        std::fprintf(stderr, "finn-counter: failed to load App.qml\n");
        return 1;
    }
    return app.exec();
}

#include "main.moc"
