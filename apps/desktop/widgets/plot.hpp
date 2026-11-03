#pragma once
#include "ui_helpers.hpp"
#ifdef AEROTRACE_QWT
class CardTimeScaleDraw final : public QwtScaleDraw {
  QwtText label(double seconds) const override {
    if(!std::isfinite(seconds) || seconds<0 || seconds>=double(UINT64_MAX)/1e9) return {};
    const auto time=stamp(std::uint64_t(std::round(seconds*1e9)));const auto dot=time.indexOf('.');return QwtText(time.left(dot)+"\n"+time.mid(dot+1));
  }
};
#endif
class Plot : public QWidget {
  QColor color;
  bool two{}, bars{};
  bool autoFollow{true};
  QPoint cursor{-1, -1};
  double zoom{1};
  QVector<QPointF> samples, secondary;
#ifdef AEROTRACE_QWT
  QwtPlot *graph{};
  QwtPlotCurve *curve{};
  QwtPlotCurve *curve2{};
#endif
public:
  std::function<void(QPointF)> onPick;
  qsizetype pointCount() const {return samples.size();}
  std::optional<QPointF> nearestSample(double seconds) const {
    if(samples.empty()) return {};
    return *std::min_element(samples.begin(),samples.end(),[seconds](const auto &a,const auto &b){return std::abs(a.x()-seconds)<std::abs(b.x()-seconds);});
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
      graph->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
      graph->setCanvasBackground(Qt::white);
      graph->canvas()->setStyleSheet("background:white;border:0;");
      graph->enableAxis(QwtPlot::yLeft, true);
      graph->setAxisMaxMajor(QwtPlot::yLeft, 4);
      graph->setAxisMaxMajor(QwtPlot::xBottom, 6);
      graph->setAxisFont(QwtPlot::xBottom, QFont("Segoe UI", 8));
      graph->setAxisScale(QwtPlot::xBottom, 0, 300);
      graph->setAxisScale(QwtPlot::yLeft, 0, 100);
      auto grid = new QwtPlotGrid;
      grid->setMajorPen(QColor("#f0f3ec"));
      grid->attach(graph);
      curve = new QwtPlotCurve;
      curve->setPen(color, 1.7);
      curve->attach(graph);
      if (two) {
        curve2 = new QwtPlotCurve;
        curve2->setPen(QColor("#a4772e"), 1.7);
        curve2->attach(graph);
      }
      auto panner=new QwtPlotPanner(graph->canvas());
      auto zoomer=new QwtPlotZoomer(graph->canvas());
      connect(panner,&QwtPlotPanner::panned,this,[this](int,int) {autoFollow=false;});
      connect(zoomer,&QwtPlotZoomer::zoomed,this,[this,zoomer](const QRectF &) {autoFollow=zoomer->zoomRectIndex()==0;});
      auto picker = new QwtPlotPicker(QwtPlot::xBottom, QwtPlot::yLeft,
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
      l->addWidget(graph);
    }
#endif
  }
  void setSeries(QVector<QPointF> points, QVector<QPointF> other = {}) {
    samples = std::move(points);
    secondary = std::move(other);
#ifdef AEROTRACE_QWT
    if (graph) {
      curve->setSamples(samples);
      if (curve2)
        curve2->setSamples(secondary);
      if(autoFollow) { graph->setAxisAutoScale(QwtPlot::xBottom); graph->setAxisAutoScale(QwtPlot::yLeft); }
      graph->replot();
      return;
    }
#endif
    update();
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
    if(samples.empty() && secondary.empty()) {
      painter.setPen(QColor("#626e60"));painter.drawText(area,Qt::AlignCenter,"Awaiting mapped data");return;
    }
    double xmin=1e300,xmax=-1e300,ymin=1e300,ymax=-1e300;
    for(const auto *series:{&samples,&secondary}) for(const auto &point:*series) {
      xmin=std::min(xmin,point.x());xmax=std::max(xmax,point.x());
      ymin=std::min(ymin,point.y());ymax=std::max(ymax,point.y());
    }
    const auto padding=std::max(.01,ymax-ymin)*.15;ymin-=padding;ymax+=padding;
    auto map=[&](const QPointF &point) {
      return QPointF(area.left()+(point.x()-xmin)/std::max(.001,xmax-xmin)*area.width(),
        area.bottom()-(point.y()-ymin)/std::max(.001,ymax-ymin)*area.height());
    };
    if(bars) for(const auto &point:samples) {
      const auto position=map(point);painter.setPen(Qt::NoPen);painter.setBrush(color);
      painter.drawRect(QRectF(position.x(),position.y(),std::max(2.,area.width()/samples.size()-3),area.bottom()-position.y()));
    } else for(int index=0;index<(two ? 2 : 1);++index) {
      const auto &series=index ? secondary : samples;QPainterPath path;
      for(int sample=0;sample<series.size();++sample) {
        if(!sample) path.moveTo(map(series[sample]));else path.lineTo(map(series[sample]));
      }
      painter.setPen(QPen(index ? QColor("#a4772e") : color,1.8));painter.drawPath(path);
    }
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

