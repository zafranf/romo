#pragma once

#include <QWidget>
#include <QPixmap>
QT_BEGIN_NAMESPACE
class QIcon;
class QLabel;
class QMovie;
QT_END_NAMESPACE

namespace Robomongo
{
    class Indicator : public QWidget
    {
        Q_OBJECT

    public:
        Indicator(const QIcon &icon, const QString &text = QString());
        void setText(const QString &text);

        /** @brief Replace the icon with an animated spinner (progress feedback). */
        void setSpinning(bool spinning);

    private:
        QLabel *createLabelWithIcon(const QIcon &icon);
        QLabel *_label;
        QLabel *_iconLabel;
        QPixmap _iconPixmap;
        QMovie *_spinner;
    };
}
