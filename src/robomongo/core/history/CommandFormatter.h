#pragma once

#include <QString>

namespace Robomongo
{
    namespace CommandFormatter
    {
        /**
         * @brief Re-indents the JSON-ish literals of a single mongo shell
         *  statement ({...} / [...]) so a one-liner reads like a formatted
         *  query, leaving the surrounding JavaScript untouched.
         *
         * db.c.find({a:1,b:2}).sort({c:-1}) becomes
         *
         *   db.c.find({
         *     a: 1,
         *     b: 2
         *   }).sort({c: -1})
         *
         * Multi-statement scripts (containing ';') and unbalanced input are
         * returned unchanged.
         */
        QString beautify(QString const &command);

        /**
         * @brief Whitespace-free form of a command, used as the identity of a
         *  history entry: the raw one-liner and its beautified version have the
         *  same key, so re-running a query never adds a second history entry.
         *  Whitespace inside string literals is preserved.
         */
        QString canonical(QString const &command);
    }
}
