/*
 * Vim conformance: every expectation here was produced by running the
 * same keys through real vim (`vim -u NONE -i NONE -N -es`, `nofixeol`)
 * and reading the bytes back, then recorded. The suite asserts the
 * editor still agrees, without needing vim installed to run.
 *
 * Both vim flags matter and both produced a wrong answer once: without
 * -i NONE viminfo carries registers between runs, and without nofixeol
 * vim appends a trailing newline that makes two identical results look
 * different. See docs/adr/0104.
 */
#include "editor_viewport.h"
#include "vim_register.h"

#include "ase/buffer.h"

#include <QApplication>
#include <QKeyEvent>
#include <QTest>

namespace {

struct Case {
    const char *name;
    const char *input;
    int line;   /* 1-based, absolute column — not "first non-blank" */
    int column;
    const char *keys;
    const char *expected;
};

/* vim's own notation, so a case reads the way it would be typed:
 * <Esc> <CR> <BS> <Tab> <Space>, and <C-x> for a control chord. */
struct ParsedKey {
    int code = 0;
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    QString text;
};

QVector<ParsedKey> parseKeys(const QString &spec) {
    QVector<ParsedKey> out;
    for (int i = 0; i < spec.size(); ++i) {
        if (spec.at(i) != QLatin1Char('<')) {
            ParsedKey key;
            key.text = QString(spec.at(i));
            key.code = spec.at(i).unicode();
            out.push_back(key);
            continue;
        }
        int close = spec.indexOf(QLatin1Char('>'), i);
        if (close < 0) {
            ParsedKey key;
            key.text = QStringLiteral("<");
            out.push_back(key);
            continue;
        }
        QString name = spec.mid(i + 1, close - i - 1);
        i = close;

        ParsedKey key;
        if (name.startsWith(QLatin1String("C-"), Qt::CaseInsensitive)) {
            QChar c = name.at(2).toLower();
            key.mods = Qt::ControlModifier;
            key.code = c.toUpper().unicode();
            /* Qt delivers the control character as the event text. */
            key.text = QString(QChar(c.unicode() - 'a' + 1));
        } else if (name.compare(QLatin1String("Esc"), Qt::CaseInsensitive) == 0) {
            key.code = Qt::Key_Escape;
        } else if (name.compare(QLatin1String("CR"), Qt::CaseInsensitive) == 0) {
            key.code = Qt::Key_Return;
            key.text = QStringLiteral("\r");
        } else if (name.compare(QLatin1String("BS"), Qt::CaseInsensitive) == 0) {
            key.code = Qt::Key_Backspace;
        } else if (name.compare(QLatin1String("Tab"), Qt::CaseInsensitive) == 0) {
            key.code = Qt::Key_Tab;
            key.text = QStringLiteral("\t");
        } else if (name.compare(QLatin1String("Space"), Qt::CaseInsensitive) == 0) {
            key.code = Qt::Key_Space;
            key.text = QStringLiteral(" ");
        } else {
            key.text = name;
            key.code = name.isEmpty() ? 0 : name.at(0).unicode();
        }
        out.push_back(key);
    }
    return out;
}

} // namespace

class VimConformance : public QObject {
    Q_OBJECT

private slots:
    void keySequences_data();
    void keySequences();
    void commands_data();
    void commands();
};

void VimConformance::keySequences_data() {
    QTest::addColumn<QString>("input");
    QTest::addColumn<int>("line");
    QTest::addColumn<int>("column");
    QTest::addColumn<QString>("keys");
    QTest::addColumn<QString>("expected");

    static const Case kCases[] = {
#include "vim_cases.inc"
    };
    for (const Case &c : kCases) {
        QTest::newRow(c.name) << QString::fromUtf8(c.input) << c.line << c.column
                              << QString::fromUtf8(c.keys) << QString::fromUtf8(c.expected);
    }
}

void VimConformance::keySequences() {
    /* Registers are process-wide; vim starts each case in a fresh
     * process, so this has to match. */
    VimRegister::clearAll();

    QFETCH(QString, input);
    QFETCH(int, line);
    QFETCH(int, column);
    QFETCH(QString, keys);
    QFETCH(QString, expected);

    QByteArray bytes = input.toUtf8();
    AseBuffer *buffer = ase_buffer_create();
    QVERIFY(buffer != nullptr);
    QVERIFY(ase_buffer_insert(buffer, 0, bytes.constData(), static_cast<size_t>(bytes.size())));

    EditorViewport viewport(buffer, QString());
    viewport.resize(800, 600);
    viewport.goToLineColumn(line, column);

    for (const ParsedKey &key : parseKeys(keys)) {
        QKeyEvent press(QEvent::KeyPress, key.code, key.mods, key.text);
        QApplication::sendEvent(&viewport, &press);
    }

    /* Read straight from the buffer, so the test needs no accessor the
     * editor would not otherwise have. */
    size_t length = ase_buffer_length(buffer);
    QByteArray actual(static_cast<int>(length), '\0');
    if (length > 0) {
        ase_buffer_get_text(buffer, 0, length, actual.data());
    }
    QCOMPARE(QString::fromUtf8(actual), expected);
}

void VimConformance::commands_data() {
    QTest::addColumn<QString>("input");
    QTest::addColumn<int>("line");
    QTest::addColumn<int>("column");
    QTest::addColumn<QString>("command");
    QTest::addColumn<QString>("expected");

    static const Case kCases[] = {
#include "vim_command_cases.inc"
    };
    for (const Case &c : kCases) {
        QTest::newRow(c.name) << QString::fromUtf8(c.input) << c.line << c.column
                              << QString::fromUtf8(c.keys) << QString::fromUtf8(c.expected);
    }
}

/* `:` commands arrive through runCommand rather than the key dispatch,
 * so they are driven directly rather than typed. */
void VimConformance::commands() {
    /* Registers are process-wide; vim starts each case in a fresh
     * process, so this has to match. */
    VimRegister::clearAll();

    QFETCH(QString, input);
    QFETCH(int, line);
    QFETCH(int, column);
    QFETCH(QString, command);
    QFETCH(QString, expected);

    QByteArray bytes = input.toUtf8();
    AseBuffer *buffer = ase_buffer_create();
    QVERIFY(buffer != nullptr);
    QVERIFY(ase_buffer_insert(buffer, 0, bytes.constData(), static_cast<size_t>(bytes.size())));

    EditorViewport viewport(buffer, QString());
    viewport.resize(800, 600);
    viewport.goToLineColumn(line, column);
    viewport.runCommand(command);

    size_t length = ase_buffer_length(buffer);
    QByteArray actual(static_cast<int>(length), '\0');
    if (length > 0) {
        ase_buffer_get_text(buffer, 0, length, actual.data());
    }
    QCOMPARE(QString::fromUtf8(actual), expected);
}

QTEST_MAIN(VimConformance)
#include "test_vim.moc"
