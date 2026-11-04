#pragma once
#include "interfaces/reader/ui/controls/panels.h"
#include "reading/bible.h"
#include <functional>
class QPushButton;
namespace kobalt::ui { class CellButton; class PagerBar; class ListHeader; }
namespace kobalt {
class BiblePicker final : public ui::Panel {
public:
    explicit BiblePicker(QWidget *parent);
    void open(const BibleInfo &info, BibleReference current);
    std::function<void(const QString &)> selected;
    std::function<void()> dismissed;
protected:
    void resizeEvent(QResizeEvent *) override;
    void paintEvent(QPaintEvent *) override;
private:
    BibleInfo info;
    BibleReference current;
    int stage = 0, book = 0, chapter = 0, page = 0;
    QString testament;
    struct Item { QString label; int value; bool checked; };
    QVector<Item> items;
    QVector<QPushButton *> cells, tabs;
    QLabel *title;
    ui::ListHeader *heading;
    ui::CellButton *back, *close;
    ui::PagerBar *pager;
    void redraw();
    void arrange();
    void pick(int value);
};
}
