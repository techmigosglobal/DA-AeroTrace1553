#pragma once
#include "ui_helpers.hpp"
#include "services/value_display.hpp"
#ifdef AEROTRACE_QWT
#include <qwt_plot_marker.h>
class CardTimeScaleDraw final : public QwtScaleDraw {
  QwtText label(double seconds) const override {
    if(!std::isfinite(seconds) || seconds<0 || seconds>=double(UINT64_MAX)/1e9) return {};
    return QwtText(desktop::displayValue(seconds)+" s");
  }
};
class EngineeringScaleDraw final : public QwtScaleDraw {
  QwtText label(double value) const override {return QwtText(desktop::displayValue(value));}
};
class GapPlotCurve final : public QwtPlotCurve {
public:
  QRectF boundingRect() const override {
    QRectF bounds(1,1,-2,-2);
    bool found=false;
    double left{},right{},top{},bottom{};
    for(size_t i=0;i<dataSize();++i) {
      const auto point=sample(i);
      if(!std::isfinite(point.y()))continue;
      if(!found){left=right=point.x();top=bottom=point.y();found=true;}
      else {left=std::min(left,point.x());right=std::max(right,point.x());top=std::min(top,point.y());bottom=std::max(bottom,point.y());}
    }
    return found?QRectF(QPointF(left,top),QPointF(right,bottom)):bounds;
  }
  void drawSeries(QPainter *painter,const QwtScaleMap &x,const QwtScaleMap &y,
                  const QRectF &canvas,int from,int to) const override {
    const auto end=to<0?int(dataSize())-1:std::min(to,int(dataSize())-1);
    auto first=std::max(from,0);
    for(int i=first;i<=end;++i) {
      if(std::isfinite(sample(i).y()))continue;
      if(first<i)QwtPlotCurve::drawSeries(painter,x,y,canvas,first,i-1);
      first=i+1;
    }
    if(first<=end)QwtPlotCurve::drawSeries(painter,x,y,canvas,first,end);
  }
};
#endif
class Plot : public QWidget {
  QColor color;
  bool two{}, bars{};
  bool autoFollow{true};
  QPointer<QLabel> statistics;
  QPoint cursor{-1, -1};
  double zoom{1};
  QVector<QPointF> samples, secondary;
  std::optional<double> lowerLimit, upperLimit;
  QLabel *statusLabel{};
#ifdef AEROTRACE_QWT
  QwtPlot *graph{};
  QwtPlotCurve *curve{};
  QwtPlotCurve *curve2{};
  QwtPlotCurve *excursions{};
  QwtPlotMarker *lowerLine{}, *upperLine{};
  QwtPlotZoomer *zoomer{};
  QwtPlotPanner *panner{};
  QwtPlotPicker *picker{};
  QList<QwtPlotMarker *> boundaries;
#endif
  bool outsideLimits(double value) const {
    return std::isfinite(value) && ((lowerLimit && value<*lowerLimit) || (upperLimit && value>*upperLimit));
  }
public:
  explicit Plot(QWidget *parent):Plot(QColor("#326344")){setParent(parent);}
  void showStatistics(QLabel *target) {statistics=target;updateStatistics();}
  const QVector<QPointF> &points() const {return samples;}
  bool followsLive() const {return autoFollow;}
  void setInteraction(int mode) {
#ifdef AEROTRACE_QWT
    if(panner)panner->setEnabled(mode==1);
    if(zoomer)zoomer->setEnabled(mode==2);
    if(picker)picker->setEnabled(mode==0);
#endif
  }
  void resetView() {
    autoFollow=true;
#ifdef AEROTRACE_QWT
    if(graph){graph->setAxisAutoScale(QwtPlot::xBottom);graph->setAxisAutoScale(QwtPlot::yLeft);graph->replot();if(zoomer)zoomer->setZoomBase();}
#endif
    updateStatistics();update();
  }
  void zoomBack() {
#ifdef AEROTRACE_QWT
    if(zoomer)zoomer->zoom(-1);
#endif
    updateStatistics();
  }
  void setAxes(double x0,double x1,std::optional<double> y0={},std::optional<double> y1={}) {
    if(!std::isfinite(x0)||!std::isfinite(x1)||x1<=x0)return;
    autoFollow=false;
#ifdef AEROTRACE_QWT
    if(graph){graph->setAxisScale(QwtPlot::xBottom,x0,x1);if(y0&&y1&&*y1>*y0)graph->setAxisScale(QwtPlot::yLeft,*y0,*y1);else graph->setAxisAutoScale(QwtPlot::yLeft);graph->replot();}
#endif
    updateStatistics();
  }
  void updateStatistics() {
    if(!statistics)return;
    double left=-INFINITY,right=INFINITY;
#ifdef AEROTRACE_QWT
    if(graph){left=graph->axisScaleDiv(QwtPlot::xBottom).lowerBound();right=graph->axisScaleDiv(QwtPlot::xBottom).upperBound();}
#endif
    double mean=0,m2=0,squares=0,lo=INFINITY,hi=-INFINITY;unsigned n=0,gaps=0;bool inGap=false;
    for(const auto &p:samples){if(p.x()<left||p.x()>right)continue;if(!std::isfinite(p.y())){if(!inGap)++gaps;inGap=true;continue;}inGap=false;++n;const auto d=p.y()-mean;mean+=d/n;m2+=d*(p.y()-mean);squares+=p.y()*p.y();lo=std::min(lo,p.y());hi=std::max(hi,p.y());}
    statistics->setText(n?QString("Visible range · %1 samples · %2 gaps\nMin %3 · Max %4 · Mean %5\nσ %6 · RMS %7 · Peak-to-peak %8").arg(n).arg(gaps)
      .arg(desktop::displayValue(lo),desktop::displayValue(hi),desktop::displayValue(mean),desktop::displayValue(std::sqrt(std::max(0.,m2/n))),desktop::displayValue(std::sqrt(squares/n)),desktop::displayValue(hi-lo)):"No valid samples in the selected range.");
  }
  std::function<void(QPointF)> onPick;
  qsizetype pointCount() const {return std::count_if(samples.begin(),samples.end(),[](const auto &p){return std::isfinite(p.y());});}
  std::optional<QPointF> nearestSample(double seconds) const {
    std::optional<QPointF> nearest;
    for(const auto &point:samples) {
      if(!std::isfinite(point.y()))continue;
      if(!nearest || std::abs(point.x()-seconds)<std::abs(nearest->x()-seconds))nearest=point;
    }
    return nearest;
  }
  void setStatus(const QString &status) {
    if(!statusLabel) {
      if(status.isEmpty())return;
      statusLabel=new QLabel(this);
      statusLabel->setObjectName("plotStatus");statusLabel->setFixedHeight(25);
      statusLabel->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
      statusLabel->setWordWrap(false);
      if(auto box=qobject_cast<QVBoxLayout *>(layout()))box->insertWidget(0,statusLabel);
      else statusLabel->hide();
    }
    if(statusLabel->text()!=status){statusLabel->setText(status);statusLabel->setToolTip(status);setAccessibleDescription(status);}
    if(layout() && statusLabel->isHidden()!=status.isEmpty())statusLabel->setVisible(!status.isEmpty());
    else if(!status.isEmpty())setToolTip(status);
  }
  void setTimeAxis(bool enabled) {
#ifdef AEROTRACE_QWT
    if(graph) {graph->setAxisScaleDraw(QwtPlot::xBottom,enabled ? static_cast<QwtScaleDraw *>(new CardTimeScaleDraw) : new QwtScaleDraw);graph->setAxisMaxMajor(QwtPlot::xBottom,enabled ? 3 : 6);}
#endif
  }
  explicit Plot(QColor c = "#326344", bool pair = false, bool bar = false)
      : color(c), two(pair), bars(bar) {
    setMinimumHeight(190);
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    setMouseTracking(true);
    setToolTip("Move to inspect • Wheel to zoom • Double-click to reset");
#ifdef AEROTRACE_QWT
    if (!bars) {
      auto l = new QVBoxLayout(this);
      l->setContentsMargins(0, 0, 0, 0);
      graph = new QwtPlot(this);
      graph->setMinimumSize(0, 0);
      graph->setObjectName("analysisCanvas");graph->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
      graph->setMinimumHeight(160);
      graph->setCanvasBackground(Qt::white);
      graph->canvas()->setStyleSheet("background:white;border:0;");
      graph->enableAxis(QwtPlot::yLeft, true);
      graph->setAxisMaxMajor(QwtPlot::yLeft, 4);
      graph->setAxisScaleDraw(QwtPlot::yLeft,new EngineeringScaleDraw);graph->axisScaleDraw(QwtPlot::yLeft)->setMinimumExtent(76);
      graph->setAxisMaxMajor(QwtPlot::xBottom, 6);
      graph->setAxisFont(QwtPlot::xBottom, QFont("Segoe UI", 9));
      graph->setAxisFont(QwtPlot::yLeft,QFont("Segoe UI",9));
      graph->setAxisScale(QwtPlot::xBottom, 0, 300);
      graph->setAxisScale(QwtPlot::yLeft, 0, 100);
      auto grid = new QwtPlotGrid;
      grid->setMajorPen(QColor("#f0f3ec"));
      grid->attach(graph);
      curve = new GapPlotCurve;
      curve->setPen(color, 1.7);
      curve->attach(graph);
      if (two) {
        curve2 = new GapPlotCurve;
        curve2->setPen(QColor("#a4772e"), 1.7);
        curve2->attach(graph);
      }
      panner=new QwtPlotPanner(graph->canvas());
      zoomer=new QwtPlotZoomer(graph->canvas());
      connect(panner,&QwtPlotPanner::panned,this,[this](int,int) {autoFollow=false;updateStatistics();});
      connect(zoomer,&QwtPlotZoomer::zoomed,this,[this](const QRectF &) {autoFollow=zoomer->zoomRectIndex()==0;updateStatistics();});
      picker = new QwtPlotPicker(QwtPlot::xBottom, QwtPlot::yLeft,
                                      QwtPicker::CrossRubberBand,
                                      QwtPicker::AlwaysOn, graph->canvas());
      picker->setStateMachine(new QwtPickerClickPointMachine);
      picker->setRubberBandPen(QColor("#626e60"));
      picker->setTrackerPen(QColor("#326344"));
      connect(picker, qOverload<const QPointF &>(&QwtPlotPicker::selected),
              this, [this](const QPointF &point) {
                if (onPick)
                  onPick(point);
              });
      l->addWidget(graph,1);setInteraction(0);
    }
#endif
  }
  void setSeries(QVector<QPointF> points, QVector<QPointF> other = {},
                 std::optional<double> minimum = {}, std::optional<double> maximum = {}) {
    if(samples==points && secondary==other && lowerLimit==minimum && upperLimit==maximum)return;
    samples.clear();samples.reserve(points.size()+16);
    std::optional<double> last;
    for(const auto &p:points){if(std::isfinite(p.y())&&last&&p.x()<*last&&(samples.empty()||std::isfinite(samples.back().y())))samples.append(QPointF(p.x(),NAN));samples.append(p);if(std::isfinite(p.y()))last=p.x();}
    secondary = std::move(other);
    lowerLimit=minimum;
    upperLimit=maximum;
#ifdef AEROTRACE_QWT
    if (graph) {
      curve->setSamples(samples);
      if (curve2)
        curve2->setSamples(secondary);
      if((lowerLimit || upperLimit) && !excursions) {
        excursions=new QwtPlotCurve("Outside configured limits");
        excursions->setStyle(QwtPlotCurve::Dots);
        excursions->setPen(QColor("#a73929"),5);
        excursions->attach(graph);
        const auto makeLimit=[this] {
          auto marker=new QwtPlotMarker;
          marker->setLineStyle(QwtPlotMarker::HLine);
          marker->setLinePen(QPen(QColor("#a4772e"),1,Qt::DashLine));
          marker->attach(graph);
          return marker;
        };
        lowerLine=makeLimit();
        upperLine=makeLimit();
      }
      if(excursions) {
        QVector<QPointF> pointsOutside;
        for(const auto &point:samples)if(outsideLimits(point.y()))pointsOutside.append(point);
        excursions->setSamples(pointsOutside);
        lowerLine->setVisible(lowerLimit.has_value() && pointCount()>0);
        upperLine->setVisible(upperLimit.has_value() && pointCount()>0);
        if(lowerLimit)lowerLine->setYValue(*lowerLimit);
        if(upperLimit)upperLine->setYValue(*upperLimit);
      }
      if(autoFollow) { graph->setAxisAutoScale(QwtPlot::xBottom); graph->setAxisAutoScale(QwtPlot::yLeft); }
      for(auto marker:boundaries){marker->detach();delete marker;}boundaries.clear();
      std::optional<double> priorTime;
      for(const auto &p:samples){if(!std::isfinite(p.y()))continue;if(priorTime && p.x()<*priorTime && boundaries.size()<32){auto marker=new QwtPlotMarker;marker->setLineStyle(QwtPlotMarker::VLine);marker->setLinePen(QPen(QColor("#a73929"),1,Qt::DashLine));marker->setXValue(p.x());if(boundaries.empty())marker->setLabel(QwtText("Time discontinuity"));marker->attach(graph);boundaries.append(marker);}priorTime=p.x();}
      setProperty("timeDiscontinuities",boundaries.size());
      graph->replot();
      updateStatistics();
      return;
    }
#endif
    updateStatistics();update();
  }
  void paintEvent(QPaintEvent *) override {
#ifdef AEROTRACE_QWT
    if(graph) return;
#endif
    QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
    const QRectF area=rect().adjusted(35,20,-12,-30);
    painter.setPen(QColor("#f0f3ec"));
    for(int line=0;line<5;++line) {
      const auto y=area.top()+area.height()*line/4;
      painter.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
    }
    if(!pointCount() && std::none_of(secondary.begin(),secondary.end(),[](const auto &p){return std::isfinite(p.y());})) {
      painter.setPen(QColor("#626e60"));painter.drawText(area,Qt::AlignCenter,"Awaiting mapped data");return;
    }
    double xmin=1e300,xmax=-1e300,ymin=1e300,ymax=-1e300;
    for(const auto *series:{&samples,&secondary}) for(const auto &point:*series) {
      if(!std::isfinite(point.y()))continue;
      xmin=std::min(xmin,point.x());xmax=std::max(xmax,point.x());
      ymin=std::min(ymin,point.y());ymax=std::max(ymax,point.y());
    }
    for(const auto limit:{lowerLimit,upperLimit}) if(limit) {
      ymin=std::min(ymin,*limit);
      ymax=std::max(ymax,*limit);
    }
    const auto padding=std::max(.01,ymax-ymin)*.15;ymin-=padding;ymax+=padding;
    auto map=[&](const QPointF &point) {
      return QPointF(area.left()+(point.x()-xmin)/std::max(.001,xmax-xmin)*area.width(),
        area.bottom()-(point.y()-ymin)/std::max(.001,ymax-ymin)*area.height());
    };
    painter.setPen(QPen(QColor("#a4772e"),1,Qt::DashLine));
    for(const auto limit:{lowerLimit,upperLimit}) if(limit) {
      const auto y=map(QPointF(xmin,*limit)).y();
      painter.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
    }
    if(bars) for(const auto &point:samples) {
      if(!std::isfinite(point.y()))continue;
      const auto position=map(point);painter.setPen(Qt::NoPen);painter.setBrush(color);
      painter.drawRect(QRectF(position.x(),position.y(),std::max(2.,area.width()/samples.size()-3),area.bottom()-position.y()));
    } else for(int index=0;index<(two ? 2 : 1);++index) {
      const auto &series=index ? secondary : samples;QPainterPath path;
      bool connected=false;
      for(const auto &point:series) {
        if(!std::isfinite(point.y())){connected=false;continue;}
        if(!connected)path.moveTo(map(point));else path.lineTo(map(point));
        connected=true;
      }
      painter.setPen(QPen(index ? QColor("#a4772e") : color,1.8));painter.drawPath(path);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#a73929"));
    for(const auto &point:samples)if(outsideLimits(point.y()))painter.drawEllipse(map(point),2.5,2.5);
    painter.setPen(QColor("#626e60"));
    painter.drawText(QPointF(area.left(),height()-8),QString::number(xmin,'f',3)+" s");
    painter.drawText(QPointF(area.right()-90,height()-8),QString::number(xmax,'f',3)+" s");
  }
  void mouseMoveEvent(QMouseEvent *e) override {
    cursor = e->position().toPoint();
    update();
  }
  void leaveEvent(QEvent *) override {
    cursor = {-1, -1};
    update();
  }
  void wheelEvent(QWheelEvent *e) override {
    zoom = qBound(.25, zoom + (e->angleDelta().y() > 0 ? -.1 : .1), 2.);
    update();
  }
  void mouseDoubleClickEvent(QMouseEvent *) override {
    zoom = 1;autoFollow=true;
    update();
  }
};

// Promoted widgets in Designer forms. Layout and sizing remain in .ui files.
class SignalPlot : public Plot {
public: explicit SignalPlot(QWidget *parent = nullptr) : Plot(QColor("#326344")) { setParent(parent); }
};
class RatePlot : public Plot {
public: explicit RatePlot(QWidget *parent = nullptr) : Plot(QColor("#326344"), true) { setParent(parent); }
};
class HistogramPlot : public Plot {
public: explicit HistogramPlot(QWidget *parent = nullptr) : Plot(QColor("#326344"), false, true) { setParent(parent); }
};
class TealPlot : public Plot {
public: explicit TealPlot(QWidget *parent = nullptr) : Plot(QColor("#a4772e")) { setParent(parent); }
};
class PurplePlot : public Plot {
public: explicit PurplePlot(QWidget *parent = nullptr) : Plot(QColor("#596353")) { setParent(parent); }
};
class OrangePlot : public Plot {
public: explicit OrangePlot(QWidget *parent = nullptr) : Plot(QColor("#b3483c")) { setParent(parent); }
};

