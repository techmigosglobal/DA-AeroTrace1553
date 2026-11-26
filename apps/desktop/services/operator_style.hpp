#pragma once

#include <QtWidgets>

namespace desktop {
inline QIcon operatorIcon(const QString &name) {
  return QIcon(":/icons/"+name+".svg");
}

inline void actionIcon(QAbstractButton *button,const QString &name,bool primary=false,int size=18) {
  auto icon=operatorIcon(name);
  if(primary) {
    auto graphic=icon.pixmap(QSize(size,size),button->devicePixelRatioF());
    QPainter tint(&graphic);
    tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
    tint.fillRect(graphic.rect(),Qt::white);
    tint.end();
    icon=QIcon(graphic);
  }
  button->setIcon(icon);
  button->setIconSize(QSize(size,size));
}

inline void labelIcon(QLabel *label,const QString &name,int size) {
  label->setPixmap(operatorIcon(name).pixmap(QSize(size,size),label->devicePixelRatioF()));
}

inline QFrame *sectionHeading(const QString &title,const QString &icon,bool blue=false,
                              const QString &description={}) {
  auto frame=new QFrame;
  frame->setProperty("sectionHeading",blue?QString("blue"):QString("true"));
  frame->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
  auto row=new QHBoxLayout(frame);
  row->setContentsMargins(11,8,11,8);
  row->setSpacing(10);
  auto image=new QLabel;
  labelIcon(image,icon,23);
  auto text=new QLabel(title);
  text->setProperty("sectionTitle",true);
  row->addWidget(image);
  auto labels=new QVBoxLayout;
  labels->setSpacing(2);
  labels->addWidget(text);
  if(!description.isEmpty()) {
    auto subtitle=new QLabel(description);
    subtitle->setProperty("noticeCopy",true);
    subtitle->setWordWrap(true);
    labels->addWidget(subtitle);
  }
  row->addLayout(labels,1);
  return frame;
}

class TableNotice final : public QWidget {
public:
  TableNotice(QAbstractItemView *view,const QString &icon,int edge)
    : QWidget(view->viewport()) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    auto layout=new QVBoxLayout(this);
    layout->setContentsMargins(12,3,12,3);
    layout->setSpacing(4);
    illustration=new QLabel(this);
    labelIcon(illustration,icon,edge);
    title->setProperty("noticeTitle",true);
    detail->setProperty("noticeCopy",true);
    detail->setAlignment(Qt::AlignCenter);
    detail->setWordWrap(true);
    layout->addStretch();
    layout->addWidget(illustration,0,Qt::AlignHCenter);
    layout->addWidget(title,0,Qt::AlignHCenter);
    layout->addWidget(detail);
    layout->addStretch();
    setGeometry(parentWidget()->rect());
    parentWidget()->installEventFilter(this);
    hide();
  }

  void showState(bool visible,const QString &heading,const QString &message) {
    if(isHidden()==visible)setVisible(visible);
    if(!visible)return;
    if(title->text()!=heading)title->setText(heading);
    if(detail->text()!=message)detail->setText(message);
  }

protected:
  bool eventFilter(QObject *watched,QEvent *event) override {
    if(watched==parentWidget() && event->type()==QEvent::Resize) {
      setGeometry(parentWidget()->rect());
      illustration->setVisible(height()>=104);
    }
    return QWidget::eventFilter(watched,event);
  }

private:
  QLabel *title=new QLabel(this);
  QLabel *detail=new QLabel(this);
  QLabel *illustration{};
};

inline void addCardShadow(QWidget *card) {
  auto shadow=new QGraphicsDropShadowEffect(card);
  shadow->setBlurRadius(11);
  shadow->setColor(QColor(28,63,88,24));
  shadow->setOffset(0,2);
  card->setGraphicsEffect(shadow);
}

// Image overlays and busy feedback share the operator's motion preference.
inline constexpr int operatorTransitionMs=150;
}
