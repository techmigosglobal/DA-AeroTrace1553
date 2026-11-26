#pragma once

#include <QtWidgets>
#include <algorithm>
#include <functional>
#include "services/runtime_paths.hpp"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace desktop {

inline bool startupMotionEnabled() {
  const QSettings settings(preferencesPath(),QSettings::IniFormat);
  return !settings.value("reduceMotion",false).toBool();
}

class StartupSplash final : public QWidget {
public:
  static constexpr int minimumVisibleMs=5000;
  int remainingMinimumDuration() const {
    return firstPaint.isValid()?int(std::max<qint64>(0,minimumVisibleMs-firstPaint.elapsed())):minimumVisibleMs;
  }
  void setStage(const QString &text) {
    if(stageText==text)return;
    stageText=text;
    update();
  }
  void finishWhenReady(std::function<void()> handoff) {
    // Measure the minimum from the first painted frame, not construction.
    // This keeps the startup check visible for the full five seconds even
    // when the first frame is delayed on a busy machine.
    if(!firstPaint.isValid()) { show(); repaint(); }
    QTimer::singleShot(remainingMinimumDuration(),Qt::PreciseTimer,this,std::move(handoff));
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
    card.setColorAt(0,QColor("#f9fcfd"));
    card.setColorAt(1,QColor("#e7f2f3"));
    painter.setBrush(card);
    painter.setPen(QColor("#a9c6c4"));
    painter.drawRoundedRect(QRectF(0.5,0.5,479,319),12,12);

    const QString version=QCoreApplication::applicationVersion();
    if(!version.isEmpty()) {
      const QString badgeText="v"+version;
      auto badgeFont=painter.font();
      badgeFont.setFamily("Segoe UI");
      badgeFont.setPixelSize(11);
      badgeFont.setWeight(QFont::DemiBold);
      painter.setFont(badgeFont);
      painter.setBrush(QColor("#e2f1ea"));
      painter.setPen(QColor("#b8d7c8"));
      painter.drawRoundedRect(QRectF(398,16,64,22),11,11);
      painter.setPen(QColor("#145331"));
      painter.drawText(QRectF(398,16,64,22),Qt::AlignCenter,badgeText);
    }

    const qint64 elapsed=animated?clock.elapsed():0;
    if(animated) {
      const double breathe=0.5-0.5*std::cos((elapsed%2000)/2000.0*6.283185307179586);
      const qreal haloRadius=44.0+6.0*breathe;
      QRadialGradient halo(240,86,haloRadius);
      halo.setColorAt(0.55,QColor(20,107,72,int(38-18*breathe)));
      halo.setColorAt(1.0,QColor(20,107,72,0));
      painter.setBrush(halo);
      painter.setPen(Qt::NoPen);
      painter.drawEllipse(QPointF(240,86),haloRadius,haloRadius);
    }

    icon.paint(&painter,QRect(202,48,76,76));
    auto font=painter.font();
    font.setFamily("Segoe UI");
    font.setPixelSize(30);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(QColor("#0b3426"));
    painter.drawText(QRectF(0,136,480,40),Qt::AlignCenter,"AeroTrace 1553");
    font.setPixelSize(14);
    font.setWeight(QFont::Normal);
    painter.setFont(font);
    painter.setPen(QColor("#2f5864"));
    painter.drawText(QRectF(24,180,432,24),Qt::AlignCenter,stageText);

    QPainterPath trace;
    trace.moveTo(110,228);
    trace.lineTo(175,228);
    trace.lineTo(183,221);
    trace.lineTo(191,235);
    trace.lineTo(199,221);
    trace.lineTo(207,235);
    trace.lineTo(215,228);
    trace.lineTo(265,228);
    trace.lineTo(273,221);
    trace.lineTo(281,235);
    trace.lineTo(289,221);
    trace.lineTo(297,235);
    trace.lineTo(305,228);
    trace.lineTo(370,228);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(animated?"#b7d3c7":"#377a62"),2.0,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    painter.drawPath(trace);

    if(animated) {
      const double phase=(elapsed%1500)/1500.0;
      const double x=100.0+280.0*phase;
      QLinearGradient pulse(x-42,0,x+42,0);
      pulse.setColorAt(0,QColor(18,110,72,0));
      pulse.setColorAt(0.45,QColor(22,135,88,210));
      pulse.setColorAt(0.55,QColor("#179862"));
      pulse.setColorAt(1,QColor(18,110,72,0));
      painter.setPen(QPen(pulse,3.2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
      painter.drawPath(trace);

      const double clampedPhase=std::clamp((x-110.0)/260.0,0.0,1.0);
      const QPointF head=trace.pointAtPercent(clampedPhase);
      if(x>=110.0 && x<=370.0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(23,152,98,70));
        painter.drawEllipse(head,7.0,7.0);
        painter.setBrush(QColor("#146b48"));
        painter.drawEllipse(head,3.5,3.5);
      }
    }

    const QRectF track(72,262,336,6);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#d6e6e1"));
    painter.drawRoundedRect(track,3,3);
    if(animated) {
      const qint64 visualMs=firstPaint.isValid()?firstPaint.elapsed():0;
      const double ratio=std::clamp(double(visualMs)/double(minimumVisibleMs),0.04,1.0);
      const qreal fillWidth=track.width()*ratio;
      const QRectF fillRect(track.left(),track.top(),fillWidth,track.height());
      QLinearGradient fillGrad(fillRect.left(),0,fillRect.right(),0);
      fillGrad.setColorAt(0,QColor("#155d40"));
      fillGrad.setColorAt(1,QColor("#249665"));
      painter.setBrush(fillGrad);
      painter.drawRoundedRect(fillRect,3,3);
    } else {
      painter.setBrush(QColor("#377a62"));
      painter.drawRoundedRect(QRectF(track.left(),track.top(),track.width()*0.5,track.height()),3,3);
    }
  }

private:
  QIcon icon;
  bool animated;
  QString stageText{"Preparing the workspace…"};
  QElapsedTimer clock;
  QElapsedTimer firstPaint;
  QTimer timer;
};
}
