#include "robomongo/core/history/CommandFormatter.h"

#include <QStringList>

namespace
{
    char const kIndent[] = "  ";

    bool isQuote(QChar c)
    {
        return c == QLatin1Char('\'') || c == QLatin1Char('"') || c == QLatin1Char('`');
    }

    /** Index just past the string literal starting at i (quotes included). */
    int skipString(QString const &s, int i)
    {
        QChar const quote = s.at(i);
        ++i;
        while (i < s.size()) {
            if (s.at(i) == QLatin1Char('\\')) {
                i += 2;
                continue;
            }
            if (s.at(i) == quote)
                return i + 1;
            ++i;
        }
        return i;
    }

    /** Collapses every whitespace run outside string literals into one space. */
    QString squash(QString const &s)
    {
        QString out;
        out.reserve(s.size());
        for (int i = 0; i < s.size();) {
            QChar const c = s.at(i);
            if (isQuote(c)) {
                int const j = skipString(s, i);
                out += s.mid(i, j - i);
                i = j;
                continue;
            }
            out += c.isSpace() ? QLatin1Char(' ') : c;
            ++i;
        }
        // collapse runs of spaces
        QString result;
        result.reserve(out.size());
        bool space = false;
        for (QChar const c : out) {
            if (c == QLatin1Char(' ')) {
                space = true;
                continue;
            }
            if (space && !result.isEmpty())
                result += QLatin1Char(' ');
            space = false;
            result += c;
        }
        return result;
    }

    /** Index of the brace/bracket closing the group opened at i, or -1. */
    int findEnd(QString const &s, int i)
    {
        QChar const open = s.at(i);
        QChar const close = open == QLatin1Char('{') ? QLatin1Char('}') : QLatin1Char(']');
        int depth = 0;
        for (int j = i; j < s.size();) {
            QChar const c = s.at(j);
            if (isQuote(c)) {
                j = skipString(s, j);
                continue;
            }
            if (c == open) {
                ++depth;
            }
            else if (c == close) {
                --depth;
                if (depth == 0)
                    return j;
            }
            ++j;
        }
        return -1;
    }

    /** Splits s on top-level commas (nested groups and strings respected). */
    QStringList splitTop(QString const &s)
    {
        QStringList parts;
        int start = 0;
        int depth = 0;
        for (int j = 0; j < s.size();) {
            QChar const c = s.at(j);
            if (isQuote(c)) {
                j = skipString(s, j);
                continue;
            }
            if (c == QLatin1Char('{') || c == QLatin1Char('[')) {
                ++depth;
            }
            else if (c == QLatin1Char('}') || c == QLatin1Char(']')) {
                --depth;
            }
            else if (c == QLatin1Char(',') && depth == 0) {
                QString const part = s.mid(start, j - start).trimmed();
                if (!part.isEmpty())
                    parts.append(part);
                start = j + 1;
            }
            ++j;
        }
        QString const last = s.mid(start).trimmed();
        if (!last.isEmpty())
            parts.append(last);
        return parts;
    }

    /** Normalises spacing of one already single-line fragment. */
    QString spacesLine(QString const &text)
    {
        QString const t = text.trimmed();
        QString out;
        out.reserve(t.size());
        auto last = [&out]() { return out.isEmpty() ? QChar() : out.at(out.size() - 1); };
        auto trimSpaces = [&out]() {
            while (!out.isEmpty() && out.at(out.size() - 1) == QLatin1Char(' '))
                out.chop(1);
        };

        for (int i = 0; i < t.size();) {
            QChar const c = t.at(i);
            if (isQuote(c)) {
                if (last() == QLatin1Char(':') || last() == QLatin1Char(','))
                    out += QLatin1Char(' ');
                int const j = skipString(t, i);
                out += t.mid(i, j - i);
                i = j;
                continue;
            }
            if (c.isSpace()) {
                QChar const p = last();
                if (!p.isNull() && p != QLatin1Char(' ') && p != QLatin1Char('\n')
                        && p != QLatin1Char('(') && p != QLatin1Char('[')
                        && p != QLatin1Char('{'))
                    out += QLatin1Char(' ');
                ++i;
                continue;
            }
            if (c == QLatin1Char(',') || c == QLatin1Char(')')
                    || c == QLatin1Char(']') || c == QLatin1Char('}')) {
                trimSpaces();
                out += c;
            }
            else if (c == QLatin1Char('(') || c == QLatin1Char('[')
                     || c == QLatin1Char('{') || c == QLatin1Char('.')) {
                // "key: {...}" keeps one space after the colon; everywhere else
                // (call parens, indexing, member access) the space is dropped.
                // Look past a space to the previous non-space char: this runs
                // over already formatted text too, so "key: {" must survive.
                int k = out.size() - 1;
                while (k >= 0 && out.at(k) == QLatin1Char(' '))
                    --k;
                bool const afterColon = k >= 0 && out.at(k) == QLatin1Char(':');
                trimSpaces();
                if (afterColon)
                    out += QLatin1Char(' ');
                out += c;
            }
            else {
                if (last() == QLatin1Char(':') || last() == QLatin1Char(','))
                    out += QLatin1Char(' ');
                out += c;
            }
            ++i;
        }
        return out.trimmed();
    }

    /** Applies spacesLine() to every line, keeping the existing indentation. */
    QString spaces(QString const &text)
    {
        QStringList lines = text.split(QLatin1Char('\n'));
        for (QString &line : lines) {
            int indentLength = 0;
            while (indentLength < line.size() && line.at(indentLength) == QLatin1Char(' '))
                ++indentLength;
            line = line.left(indentLength) + spacesLine(line.mid(indentLength));
        }
        QString result = lines.join(QLatin1Char('\n'));
        while (result.endsWith(QLatin1Char('\n')) || result.endsWith(QLatin1Char(' ')))
            result.chop(1);
        result.replace(QStringLiteral("},{"), QStringLiteral("}, {"));
        result.replace(QStringLiteral("],{"), QStringLiteral("], {"));
        return result;
    }

    QString formatGroup(QString const &s, int a, int b, int ind);

    /** Formats a fragment, recursing into every balanced group it contains. */
    QString formatValue(QString const &segment, int ind)
    {
        QString const seg = squash(segment);
        QString out;
        for (int i = 0; i < seg.size();) {
            QChar const c = seg.at(i);
            if (isQuote(c)) {
                int const j = skipString(seg, i);
                out += seg.mid(i, j - i);
                i = j;
                continue;
            }
            if (c == QLatin1Char('{') || c == QLatin1Char('[')) {
                int const j = findEnd(seg, i);
                if (j < 0) {
                    out += c;
                    ++i;
                    continue;
                }
                out += formatGroup(seg, i, j, ind);
                i = j + 1;
                continue;
            }
            out += c;
            ++i;
        }
        return spaces(out);
    }

    QString formatGroup(QString const &s, int a, int b, int ind)
    {
        QString const body = squash(s.mid(a + 1, b - a - 1));
        QStringList const parts = splitTop(body);

        if (parts.isEmpty())
            return QString(s.at(a)) + s.at(b);

        // Every non-empty literal is expanded, single-key objects included:
        // find({project: 5003}) must read as a formatted query, not stay on
        // one line just because it happens to hold a single key.

        QStringList lines;
        for (QString const &part : parts)
            lines.append(QString(kIndent).repeated(ind + 1) + formatValue(part, ind + 1));

        return QString(s.at(a)) + QLatin1Char('\n') + lines.join(QStringLiteral(",\n"))
               + QLatin1Char('\n') + QString(kIndent).repeated(ind) + s.at(b);
    }
}

namespace Robomongo
{
    namespace CommandFormatter
    {
        QString beautify(QString const &command)
        {
            QString const text = command.trimmed();
            if (text.isEmpty())
                return command;

            // Multi-statement scripts are left alone: re-indenting across
            // statements is out of scope and only risks mangling the code.
            if (text.contains(QLatin1Char(';')))
                return command;
            if (!text.contains(QLatin1Char('{')) && !text.contains(QLatin1Char('[')))
                return command;

            return formatValue(text, 0);
        }

        QString canonical(QString const &command)
        {
            QString out;
            out.reserve(command.size());
            for (int i = 0; i < command.size();) {
                QChar const c = command.at(i);
                if (isQuote(c)) {
                    int const j = skipString(command, i);
                    out += command.mid(i, j - i);
                    i = j;
                    continue;
                }
                if (!c.isSpace())
                    out += c;
                ++i;
            }
            return out;
        }
    }
}
