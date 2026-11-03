#include "ArchiveModel.h"
#include "ArchiveWindow.h"
#include "Archiver.h"
#include "CreateDialog.h"
#include "Jobs.h"
#include "Lang.h"
#include "Shared.h"
#include "Theme.h"
#include "Thermometer.h"

#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QTimer>
#include <QTreeView>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace packix;
using namespace packix::ui;

namespace {

void flatten(const QJsonObject& obj, const QString& prefix, QHash<QString, QJsonValue>& out)
{
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QString key = prefix.isEmpty() ? it.key() : prefix + QLatin1Char('.') + it.key();
        if (it->isObject())
            flatten(it->toObject(), key, out);
        else
            out.insert(key, *it);
    }
}

} // namespace

class TestUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QDir(Lang::instance().userDir()).removeRecursively();
        QDir().mkpath(Lang::instance().userDir());
        QVERIFY(Lang::instance().setLanguage(QStringLiteral("ru")));
    }

    // Every L("...") / Ln("...") in the sources has a Russian text; plural ones are arrays.
    void allKeysTranslated()
    {
        QHash<QString, QJsonValue> ru;
        flatten(Shared::loadFile(Shared::path(QStringLiteral("lang/ru.json"))), QString(), ru);
        QVERIFY(!ru.isEmpty());

        const QRegularExpression call(QStringLiteral("\\b(Ln?)\\(\"([A-Za-z0-9_.]+)\""));
        QStringList missing;
        int checked = 0;
        for (const QString& dir : {QStringLiteral("app"), QStringLiteral("dialogs")}) {
            QDirIterator it(QStringLiteral(PACKIX_SOURCE_DIR "/") + dir, {QStringLiteral("*.cpp")}, QDir::Files);
            while (it.hasNext()) {
                QFile f(it.next());
                QVERIFY(f.open(QIODevice::ReadOnly));
                const QString text = QString::fromUtf8(f.readAll());
                for (auto m = call.globalMatch(text); m.hasNext();) {
                    const auto match = m.next();
                    const QString key = match.captured(2);
                    ++checked;
                    if (!ru.contains(key))
                        missing << key;
                    else if (match.captured(1) == QLatin1String("Ln") && !ru.value(key).isArray())
                        missing << key + QStringLiteral(" (needs plural forms)");
                }
            }
        }
        for (const char* level : {"store", "fast", "normal", "max"})
            QVERIFY(ru.contains(QStringLiteral("level.") + QLatin1String(level)));
        QVERIFY(checked > 100);
        QVERIFY2(missing.isEmpty(), qPrintable(missing.join(QStringLiteral(", "))));

        // Type descriptions named in file-types.json.
        const QJsonObject types = Shared::load(QStringLiteral("file-types.json"));
        for (const QJsonValue& g : types.value(QStringLiteral("groups")).toArray()) {
            const QString desc = g.toObject().value(QStringLiteral("desc")).toString();
            QVERIFY2(desc.isEmpty() || ru.contains(desc), qPrintable(desc));
        }
    }

    void englishComplete()
    {
        QHash<QString, QJsonValue> ru, en;
        flatten(Shared::loadFile(Shared::path(QStringLiteral("lang/ru.json"))), QString(), ru);
        flatten(Shared::loadFile(Shared::path(QStringLiteral("lang/en.json"))), QString(), en);
        QStringList problems;
        for (auto it = ru.cbegin(); it != ru.cend(); ++it) {
            if (!en.contains(it.key()))
                problems << it.key();
            else if (it->isArray() && en.value(it.key()).toArray().size() != 2)
                problems << it.key() + QStringLiteral(" (needs 2 forms)");
        }
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral(", "))));
        QCOMPARE(en.value(QStringLiteral("_meta.plural")).toString(), QStringLiteral("one"));
    }

    void russianPlurals()
    {
        QCOMPARE(Ln("files.count", 1), QStringLiteral("1 файл"));
        QCOMPARE(Ln("files.count", 2), QStringLiteral("2 файла"));
        QCOMPARE(Ln("files.count", 5), QStringLiteral("5 файлов"));
        QCOMPARE(Ln("files.count", 11), QStringLiteral("11 файлов"));
        QCOMPARE(Ln("files.count", 21), QStringLiteral("21 файл"));
        QCOMPARE(Ln("files.count", 22), QStringLiteral("22 файла"));
        QCOMPARE(Ln("files.count", 112), QStringLiteral("112 файлов"));
        QCOMPARE(L("conflict.keepBoth"), QStringLiteral("Оставить оба"));
        QCOMPARE(L("extract.subfolder", {{"name", QStringLiteral("Фото")}}), QStringLiteral("Создать папку «Фото»"));
        QCOMPARE(L("no.such.key"), QStringLiteral("no.such.key"));
    }

    void userLanguages()
    {
        const QString dir = Lang::instance().userDir();
        QFile good(dir + QStringLiteral("/en.json"));
        QVERIFY(good.open(QIODevice::WriteOnly));
        good.write(R"({"_meta": {"name": "English", "plural": "one"}, "files": {"count": ["{n} file", "{n} files"]},
                       "common": {"cancel": "Cancel"}})");
        good.close();
        QFile bad(dir + QStringLiteral("/xx.json"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{\n  \"_meta\": {\"name\": \"Broken\"},\n  \"a\": \"b\",,\n}\n");
        bad.close();

        // Picked up without a restart.
        QTRY_VERIFY_WITH_TIMEOUT(Lang::instance().available().size() >= 3, 5000);
        bool sawBroken = false;
        for (const Lang::Info& i : Lang::instance().available()) {
            if (i.code == QLatin1String("xx")) {
                sawBroken = true;
                QVERIFY(!i.error.isEmpty());
                QCOMPARE(i.errorLine, 3);
            }
        }
        QVERIFY(sawBroken);
        QVERIFY(!Lang::instance().setLanguage(QStringLiteral("xx")));

        QVERIFY(Lang::instance().setLanguage(QStringLiteral("en")));
        QCOMPARE(Ln("files.count", 1), QStringLiteral("1 file"));
        QCOMPARE(Ln("files.count", 3), QStringLiteral("3 files"));
        QCOMPARE(L("common.cancel"), QStringLiteral("Cancel"));
        QCOMPARE(L("conflict.keepBoth"), QStringLiteral("Оставить оба"));   // missing in en: Russian
        QCOMPARE(Ln("delete.textMany", 5).left(10), QStringLiteral("5 объектов"));
        QVERIFY(Lang::instance().setLanguage(QStringLiteral("ru")));
    }

    void themeColors()
    {
        for (const char* c : {"accent", "tint", "selection", "focus", "glass", "glassEdge", "liquidTop", "liquidBottom", "dropZone"})
            QVERIFY2(Theme::get().color(c).isValid(), c);
        QCOMPARE(Theme::get().color("glassEdge").alpha(), 0x24);
        QCOMPARE(Theme::get().motion("packMs", 0), 250);
    }

    void createDialog()
    {
        QTemporaryDir tmp;
        CreateDialog dlg({tmp.filePath(QStringLiteral("a.txt"))});
        const auto edits = dlg.findChildren<QLineEdit*>();
        QLineEdit* name = edits.at(0);
        QLineEdit* pass = edits.at(1);
        QLineEdit* again = edits.at(2);
        QComboBox* format = dlg.findChildren<QComboBox*>().at(0);
        QComboBox* level = dlg.findChildren<QComboBox*>().at(1);
        QPushButton* create = nullptr;
        for (QPushButton* b : dlg.findChildren<QPushButton*>()) {
            if (b->text() == L("create.button"))
                create = b;
        }
        QVERIFY(create);

        QCOMPARE(format->currentData().toString(), QStringLiteral("zip"));
        QCOMPARE(level->currentData().toString(), QStringLiteral("normal"));
        QVERIFY(name->text().isEmpty());
        QVERIFY(!create->isEnabled());

        name->setText(QStringLiteral("Фото"));
        QVERIFY(create->isEnabled());
        pass->setText(QStringLiteral("секрет"));
        again->setText(QStringLiteral("секрет"));
        QVERIFY(!create->isEnabled());   // zip: Latin only
        pass->setText(QStringLiteral("abc"));
        again->setText(QStringLiteral("abd"));
        QVERIFY(!create->isEnabled());   // mismatch
        again->setText(QStringLiteral("abc"));
        QVERIFY(create->isEnabled());

        // Another type: the fields go away and forget what was typed.
        format->setCurrentIndex(format->findData(QStringLiteral("7z")));
        QVERIFY(pass->text().isEmpty());
        QVERIFY(again->text().isEmpty());
        QVERIFY(create->isEnabled());
        format->setCurrentIndex(format->findData(QStringLiteral("zip")));
        QVERIFY(pass->text().isEmpty());

        name->setText(QStringLiteral("a/b"));
        QVERIFY(!create->isEnabled());
    }

    // What the user does: select a file, press ⌫, confirm — the file must be gone.
    void deleteFromArchiveWindow_data()
    {
        QTest::addColumn<QString>("format");
        QTest::addColumn<bool>("byKey");
        QTest::newRow("zip, key") << "zip" << true;
        QTest::newRow("zip, button") << "zip" << false;
        QTest::newRow("7z, key") << "7z" << true;
        QTest::newRow("7z, button") << "7z" << false;
    }

    void deleteFromArchiveWindow()
    {
        QFETCH(QString, format);
        QFETCH(bool, byKey);
        QTemporaryDir tmp;
        const QString src = tmp.filePath(QStringLiteral("Папка"));
        QDir().mkpath(src);
        for (const char* n : {"a.txt", "b.txt"}) {
            QFile f(src + QLatin1Char('/') + QLatin1String(n));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("data");
        }
        Archiver::CreateRequest r;
        r.sources = {src};
        r.destinationDir = tmp.path();
        r.fileName = QStringLiteral("t.") + format;
        r.formatId = format;
        r.levelId = QStringLiteral("normal");
        QString archive;
        QVERIFY(Archiver::create(r, nullptr, &archive).ok());

        auto* w = new ArchiveWindow(archive);
        QVERIFY(w->start());
        auto* view = w->findChild<QTreeView*>();
        auto* model = qobject_cast<ArchiveModel*>(view->model());
        const QModelIndex file = model->indexOfPath(QStringLiteral("Папка/a.txt"));
        QVERIFY(file.isValid());
        view->setFocus();
        view->selectionModel()->select(file, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        view->setCurrentIndex(file);

        // Press the destructive button of the confirmation as soon as it appears.
        bool confirmed = false;
        QTimer poll;
        connect(&poll, &QTimer::timeout, this, [&] {
            for (QWidget* top : QApplication::topLevelWidgets()) {
                auto* box = qobject_cast<QMessageBox*>(top);
                if (!box) {
                    for (QMessageBox* b : top->findChildren<QMessageBox*>())
                        box = b;
                }
                if (!box || !box->isVisible())
                    continue;
                for (QAbstractButton* b : box->buttons()) {
                    if (box->buttonRole(b) == QMessageBox::DestructiveRole) {
                        confirmed = true;
                        b->click();
                        return;
                    }
                }
            }
        });
        poll.start(50);
        QPushButton* del = nullptr;
        for (QPushButton* b : w->findChildren<QPushButton*>()) {
            if (b->text() == L("archive.delete"))
                del = b;
        }
        QVERIFY(del && del->isEnabled());
        if (byKey)
            QTest::keyClick(view, Qt::Key_Backspace);
        else
            del->click();
        QTRY_VERIFY_WITH_TIMEOUT(confirmed, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(Jobs::instance().running().isEmpty(), 10000);
        poll.stop();

        ArchiveInfo info;
        QVERIFY(Archiver::open(archive, {}, info).ok());
        QStringList names;
        for (const Item& i : info.items)
            names << i.path;
        QVERIFY2(!names.contains(QStringLiteral("Папка/a.txt")), qPrintable(names.join(QStringLiteral(", "))));
        QTRY_VERIFY(!model->indexOfPath(QStringLiteral("Папка/a.txt")).isValid());   // the window shows it too
        QVERIFY(model->indexOfPath(QStringLiteral("Папка/b.txt")).isValid());
        w->close();
    }

    void thermometer()
    {
        Thermometer t;
        t.resize(300, 32);
        t.setValue(-1);
        t.setValue(0.5);
        bool done = false;
        t.finish([&] { done = true; });
        QTRY_VERIFY(done);   // hidden: completes at once
        QImage img(t.size(), QImage::Format_ARGB32);
        t.render(&img);
    }
};

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
