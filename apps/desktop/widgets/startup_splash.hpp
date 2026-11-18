#pragma once

#include <QtWidgets>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace desktop {

inline bool startupMotionEnabled() {
#ifdef Q_OS_WIN
  BOOL enabled=FALSE;
  return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0) && enabled;
#else
  return true;
#endif
}

class StartupSplash final : public QWidget {
public:
  static constexpr int minimumVisibleMs=5000;
  int remainingMinimumDuration() const {
    return firstPaint.isValid()?int(std::max<qint64>(0,minimumVisibleMs-firstPaint.elapsed())):minimumVisibleMs;
  }
  qint64 visibleElapsed() const {return firstPaint.isValid()?firstPaint.elapsed():0;}
  explicit StartupSplash(bool animate)
    : QWidget(nullptr,Qt::SplashScreen | Qt::FramelessWindowHint),
      icon(":/icons/aerotrace.ico"), animated(animate) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setWindowTitle("Starting AeroTrace");
    setAccessibleName("Starting AeroTrace");
    resize(480,320);
    if(const auto screen=QGuiApplication::primaryScreen()) {
      const auto area=screen->availableGeometry();
      const auto factor=std::min({1.0,area.width()/520.0,area.height()/360.0});
      resize(qRound(480*factor),qRound(320*factor));
      move(area.center()-rect().center());
    }
    clock.start();
    if(animated) {
      connect(&timer,&QTimer::timeout,this,qOverload<>(&QWidget::update));
      timer.start(16);
    }
  }

protected:
  void paintEvent(QPaintEvent *) override {
    if(!firstPaint.isValid())firstPaint.start();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(width()/480.0,height()/320.0);

    QLinearGradient card(0,0,480,320);
    card.setColorAt(0,QColor("#233a32"));
    card.setColorAt(1,QColor("#172922"));
    painter.setBrush(card);
    painter.setPen(QColor("#526a60"));
    painter.drawRoundedRect(QRectF(0.5,0.5,479,319),12,12);

    icon.paint(&painter,QRect(202,64,76,76));
    auto font=painter.font();
    font.setFamily("Segoe UI");
    font.setPixelSize(30);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(QColor("#f4f6f0"));
    painter.drawText(QRectF(0,155,480,42),Qt::AlignCenter,"AeroTrace 1553");
    font.setPixelSize(14);
    font.setWeight(QFont::Normal);
    painter.setFont(font);
    painter.setPen(QColor("#c5d8ce"));
    painter.drawText(QRectF(0,201,480,24),Qt::AlignCenter,"Starting AeroTrace");

    QPainterPath trace;
    trace.moveTo(164,248);
    trace.lineTo(197,248);
    trace.lineTo(204,243);
    trace.lineTo(211,253);
    trace.lineTo(218,243);
    trace.lineTo(225,253);
    trace.lineTo(232,248);
    trace.lineTo(316,248);
    painter.setPen(QPen(QColor(animated?"#668577":"#a6ecc6"),1.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    painter.drawPath(trace);
    if(animated) {
      const auto x=144.0+192.0*(clock.elapsed()%1650)/1650.0;
      QLinearGradient pulse(x-18,0,x+18,0);
      pulse.setColorAt(0,QColor(166,236,198,0));
      pulse.setColorAt(0.5,QColor("#a6ecc6"));
      pulse.setColorAt(1,QColor(166,236,198,0));
      painter.setPen(QPen(pulse,1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
      painter.drawPath(trace);
    }
  }

private:
  QIcon icon;
  bool animated;
  QElapsedTimer clock;
  QElapsedTimer firstPaint;
  QTimer timer;
};
}
