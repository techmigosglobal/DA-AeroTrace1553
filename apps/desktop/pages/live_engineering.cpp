#include "main_window.hpp"
#include "services/manual_mapping.hpp"
#include "services/operator_style.hpp"
#include <QPainterPath>
#include <cmath>

namespace {
class LiveFields final : public QAbstractListModel {
public:
  using QAbstractListModel::QAbstractListModel;
  std::vector<aero::Signal> profile;
  std::vector<unsigned> fields;
  std::shared_ptr<const aero::LiveFrame> frame;
  aero::CommandPlotSelection request;
  bool running{};
  int rowCount(const QModelIndex &parent={}) const override {return parent.isValid()?0:int(fields.size());}
  void select(const QString &command) {
    beginResetModel();
    fields.clear();frame.reset();
    for(unsigned i=0;i<profile.size();++i)
      if(QString::fromStdString(aero::signalCommandKey(profile[i]))==command) fields.push_back(i);
    endResetModel();
  }
  const aero::LiveValue *value(int row) const {
    if(!running || !frame)return nullptr;
    const auto &values=frame->sourceValues[(request.channel-1)*2+request.bus];
    return fields[row]<values.size() && values[fields[row]].matched?&values[fields[row]]:nullptr;
  }
  QString title(int row) const {
    const auto &s=profile[fields[row]];
    return QString::fromStdString(s.sourceSignalId.empty()?s.id:s.sourceSignalId);
  }
  QString details(int row) const {
    const auto *current=value(row);
    if(!current)return running?"Waiting for matching data":"Recording stopped";
    if(!current->value)return current->reason.empty()?QString("Invalid sample"):
      QString::fromUtf8(current->reason.data(),qsizetype(current->reason.size()));
    const auto &signal=profile[fields[row]];
    return QString("%1 %2 · Valid · %3").arg(QString::number(*current->value,'g',12),
      QString::fromStdString(signal.unit),stamp(current->timestampNs));
  }
  QVariant data(const QModelIndex &index,int role) const override {
    if(!index.isValid() || index.row()>=rowCount())return {};
    if(role==Qt::AccessibleTextRole || role==Qt::DisplayRole || role==Qt::ToolTipRole)
      return title(index.row())+" · "+details(index.row());
    return {};
  }
  void refresh(std::shared_ptr<const aero::LiveFrame> next,bool active,int first,int last) {
    frame=std::move(next);running=active;
    if(first>=0 && first<rowCount())
      emit dataChanged(index(first),index(qMin(last,rowCount()-1)));
  }
};

class ChartRows final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  int rowHeight{106};
  QSize sizeHint(const QStyleOptionViewItem &,const QModelIndex &) const override {return {320,rowHeight};}
  void paint(QPainter *painter,const QStyleOptionViewItem &option,const QModelIndex &index) const override {
    const auto &model=*static_cast<const LiveFields *>(index.model());
    const auto row=index.row();
    painter->save();painter->setClipRect(option.rect);
    const auto selected=option.state.testFlag(QStyle::State_Selected);
    painter->fillRect(option.rect,selected?option.palette.alternateBase():option.palette.base());
    painter->setPen(option.palette.text().color());
    const auto box=option.rect.adjusted(12,5,-12,-5);
    painter->drawText(box,Qt::AlignTop|Qt::AlignLeft,model.title(row));
    painter->drawText(box,Qt::AlignTop|Qt::AlignRight,model.details(row));
    const auto plot=model.frame && model.frame->commandPlot?&*model.frame->commandPlot:nullptr;
    if(plot && model.running && plot->selection.command==model.request.command &&
       plot->selection.channel==model.request.channel && plot->selection.bus==model.request.bus &&
       unsigned(row)>=plot->selection.first && unsigned(row)-plot->selection.first<plot->traces.size()) {
      const auto &samples=plot->traces[unsigned(row)-plot->selection.first];
      double minimum=INFINITY,maximum=-INFINITY;
      for(const auto &sample:samples)if(std::isfinite(sample.second)) {
        minimum=std::min(minimum,sample.second);maximum=std::max(maximum,sample.second);
      }
      if(minimum<=maximum) {
        const QRectF axes=box.adjusted(85,23,0,-5);
        painter->setPen(option.palette.mid().color());
        painter->drawRect(axes);
        painter->setPen(option.palette.text().color());
        const QRect upper(box.left(),int(axes.top()),80,20);
        const QRect lower(box.left(),int(axes.bottom())-20,80,20);
        painter->drawText(upper,Qt::AlignVCenter,QString::number(maximum,'g',6));
        if(maximum!=minimum)painter->drawText(lower,Qt::AlignVCenter,QString::number(minimum,'g',6));
        painter->setClipRect(axes.adjusted(-1,-1,1,1),Qt::IntersectClip);
        QPainterPath path;
        std::optional<QPointF> previous;
        bool horizontal=false;
        const auto flushHorizontal=[&] {
          if(horizontal && previous)path.lineTo(*previous);
          horizontal=false;
        };
        painter->setPen(QPen(QColor("#23654a"),1.5));
        for(const auto &[time,value]:samples) {
          if(!std::isfinite(value)){flushHorizontal();previous.reset();continue;}
          const auto range=maximum-minimum;
          const auto y=maximum==minimum?.5:std::isfinite(range)?(value-minimum)/range:
            (value/2-minimum/2)/(maximum/2-minimum/2);
          const QPointF point(axes.right()-(plot->newest-time)*axes.width()/60.,axes.bottom()-y*axes.height());
          if(!previous) {
            path.moveTo(point);painter->drawPoint(point);
          } else if(point.y()==previous->y()) {
            horizontal=true;
          } else {
            flushHorizontal();
            path.lineTo(point);
          }
          previous=point;
        }
        flushHorizontal();
        painter->drawPath(path);
        painter->setClipRect(option.rect);
      }
    }
    if(option.state.testFlag(QStyle::State_HasFocus)) {
      painter->setPen(QPen(option.palette.highlight().color(),2));
      painter->drawRect(option.rect.adjusted(1,1,-2,-2));
    }
    painter->restore();
  }
};
}

void Window::liveEngineering() {
  const auto record=selectedMonitorRecord();
  auto existing=findChild<QDialog *>("liveEngineeringDialog");
  if(existing)existing->close();
  auto dialog=new QDialog(this);
  dialog->setObjectName("liveEngineeringDialog");dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("Live Engineering");
  const auto available=screen()->availableGeometry();
  dialog->resize(qMin(1000,available.width()-36),qMin(680,available.height()-45));
  auto layout=new QVBoxLayout(dialog);
  auto controls=new QHBoxLayout;layout->addLayout(controls);
  auto records=new QPushButton("Bus records");
  auto focus=new QPushButton("Focus view");
  auto stop=new QPushButton("Stop && save");
  stop->setProperty("primary",true);
  desktop::actionIcon(records,"columns");
  desktop::actionIcon(focus,"expand");
  desktop::actionIcon(stop,"save",true);
  controls->addWidget(records);controls->addWidget(focus);controls->addWidget(stop);
  connect(stop,&QPushButton::clicked,this,[this]{stopAndSave();});
  connect(records,&QPushButton::clicked,dialog,[this,dialog] {
    if(!monitorFocused) {
      if(page!=4)navigate(4);
      if(auto button=content->findChild<QPushButton *>("monitorFocusMode"))button->click();
    } else if(page!=4)navigate(4);
    dialog->close();
  });
  auto routes=new QComboBox;routes->setObjectName("liveCommand");
  auto streams=new QComboBox;streams->setObjectName("liveStream");
  streams->addItems({"CH1 / A","CH1 / B","CH2 / A","CH2 / B"});
  for(const auto &[caption,input]:std::array<std::pair<QString,QComboBox *>,2>{{{"Command",routes},{"Stream",streams}}}) {
    auto label=new QLabel(caption,dialog);label->setBuddy(input);input->setAccessibleName(caption);
    controls->addWidget(label);controls->addWidget(input,input==routes?1:0);
  }
  auto summary=new QLabel(dialog);summary->setObjectName("liveEngineeringStatus");summary->setWordWrap(true);layout->addWidget(summary);
  auto chart=new QListView(dialog);chart->setObjectName("liveFieldCharts");
  chart->setAccessibleName("Live values and synchronized field plots");
  chart->setUniformItemSizes(true);
  auto fields=new LiveFields(chart);fields->profile=backend.mappings();
  chart->setModel(fields);
  auto delegate=new ChartRows(chart);
  chart->setItemDelegate(delegate);
  auto compactTable=new QTableWidget(0,5,dialog);
  compactTable->setObjectName("liveCompactTable");
  compactTable->setHorizontalHeaderLabels({"Signal","Raw Word","Engineering Value","Unit","Timestamp"});
  compactTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  compactTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  compactTable->setAlternatingRowColors(true);
  compactTable->verticalHeader()->hide();
  compactTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  compactTable->horizontalHeader()->setStretchLastSection(true);
  compactTable->hide();
  auto viewToggle=new QPushButton("Compact Table",dialog);
  viewToggle->setObjectName("liveViewModeToggle");
  viewToggle->setCheckable(true);
  desktop::actionIcon(viewToggle,"columns");
  controls->insertWidget(2,viewToggle);
  connect(viewToggle,&QPushButton::toggled,dialog,[viewToggle,chart,compactTable](bool tableMode){
    chart->setVisible(!tableMode);
    compactTable->setVisible(tableMode);
    viewToggle->setText(tableMode?"Strip Charts":"Compact Table");
  });
  connect(focus,&QPushButton::clicked,dialog,[focus,dialog,chart,delegate] {
    const bool entering=!dialog->isMaximized();
    const auto current=chart->indexAt(QPoint(4,4));
    delegate->rowHeight=entering?234:106;
    if(entering)dialog->showMaximized();
    else dialog->showNormal();
    focus->setText(entering?"Exit focus":"Focus view");
    desktop::actionIcon(focus,entering?"collapse":"expand");
    chart->doItemsLayout();
    if(current.isValid())chart->scrollTo(current,QAbstractItemView::PositionAtTop);
  });
  layout->addWidget(chart,1);
  layout->addWidget(compactTable,1);
  auto legend=new QLabel("Time on selected stream · 60 s ago → latest",dialog);layout->addWidget(legend);
  auto mappings=new QPushButton("Edit in Signal Mappings →",dialog);desktop::actionIcon(mappings,"edit");layout->addWidget(mappings,0,Qt::AlignLeft);
  connect(mappings,&QPushButton::clicked,this,[this,dialog]{dialog->close();navigate(12);});
  const auto protocol=record?std::optional{aero::interpret(*record)}:std::nullopt;
  QSet<QString> seen;
  int matched=-1;
  for(const auto &signal:fields->profile) {
    const auto identifier=QString::fromStdString(aero::signalCommandKey(signal));
    if(seen.contains(identifier))continue;
    seen.insert(identifier);
    QJsonObject route{{"rt",int(signal.rt)},{"sa",int(signal.sa)},{"count",int(signal.expectedWordCount)},{"tx",signal.transmit}};
    if(signal.matchWord>=0){route["match_word"]=signal.matchWord;route["match_value"]=int(signal.matchValue);}
    const auto prefix=signal.subsystem.empty()?QString("Active ICD"):QString::fromStdString(signal.subsystem);
    routes->addItem(prefix+" · "+desktop::manualRouteLabel(route),identifier);
    if(protocol && !protocol->rtToRt && !protocol->mode &&
       aero::matchesSignalRoute(*record,signal,protocol->payloadCommand) &&
       (!signal.expectedWordCount || (protocol->payloadCommand.count==signal.expectedWordCount && record->words.size()==signal.expectedWordCount)))
      matched=routes->count()-1;
  }
  if(matched>=0) {
    routes->setCurrentIndex(matched);
    streams->setCurrentIndex((record->channel-1)*2+record->bus);
  } else if(record && routes->count()) {
    routes->insertItem(0,"Selected record has no active mapping",QString());
    routes->setCurrentIndex(0);
  } else if(routes->count()) {
    const auto &signal=fields->profile.front();
    streams->setCurrentIndex((signal.channel?signal.channel-1:0)*2+qMax(0,signal.bus));
  }
  if(monitorFocused && liveFocusStream>=0) {
    const auto previous=routes->findData(liveFocusCommand);
    if(previous>=0){routes->setCurrentIndex(previous);streams->setCurrentIndex(liveFocusStream);}
  }
  auto poll=[this,routes,streams,chart,compactTable,fields,summary,stop,delegate] {
    const auto latestProfile=backend.mappings();
    if(latestProfile.size()!=fields->profile.size()) {
      fields->profile=latestProfile;
      fields->select(routes->currentData().toString());
    }
    const auto stream=qMax(0,streams->currentIndex());
    const int first=qMax(0,chart->indexAt(QPoint(4,4)).row());
    const auto visible=qBound(1,chart->viewport()->height()/delegate->rowHeight+2,24);
    fields->request={routes->currentData().toString().toStdString(),unsigned(stream/2+1),
      unsigned(stream%2),unsigned(first),unsigned(visible)};
    backend.acquisition.setCommandPlot(fields->request);
    const auto active=backend.acquisition.active();
    stop->setEnabled(active&&!savingSession);
    if(monitorFocused){liveFocusCommand=routes->currentData().toString();liveFocusStream=stream;}
    fields->refresh(backend.acquisition.liveFrame(),active,first,first+visible-1);
    if(compactTable->isVisible()) {
      const int rows=fields->rowCount();
      compactTable->setRowCount(rows);
      for(int r=0;r<rows;++r) {
        const auto &sig=fields->profile[fields->fields[r]];
        const auto *current=fields->value(r);
        auto setCell=[&](int col,const QString &txt){
          auto item=compactTable->item(r,col);
          if(!item){item=new QTableWidgetItem;compactTable->setItem(r,col,item);}
          if(item->text()!=txt)item->setText(txt);
        };
        setCell(0,fields->title(r));
        setCell(1,QString("Word %1").arg(sig.word));
        setCell(2,current && current->value ? QString::number(*current->value,'g',12) : QString("—"));
        setCell(3,QString::fromStdString(sig.unit));
        setCell(4,current ? (current->value ? "VALID · "+stamp(current->timestampNs) : QString::fromUtf8(current->reason.data(),qsizetype(current->reason.size()))) : (active ? "Waiting for data" : "Recording stopped"));
      }
    }
    const auto note=!routes->count()?QString("Publish or import an active ICD command to view its values and plots."):
      fields->request.command.empty()?QString("Choose an active command to see its engineering values."):
      QString("%1 fields · %2 · %3").arg(fields->rowCount()).arg(streams->currentText(),
        active?"Recording":"Start recording to see live values.");
    if(summary->text()!=note)summary->setText(note);
    if(fields->request.command.empty())return;
    const auto snapshot=fields->frame&&fields->frame->commandPlot?&*fields->frame->commandPlot:nullptr;
    int plotted=0;
    if(snapshot && snapshot->selection==fields->request)for(const auto &traces:snapshot->traces)plotted+=!traces.empty();
    chart->setProperty("visibleTracesWithData",plotted);
  };
  auto select=[routes,fields,chart,poll]{fields->select(routes->currentData().toString());chart->scrollToTop();poll();};
  auto timer=new QTimer(dialog);timer->setInterval(150);
  const auto monitorInterval=tick.interval();
  // The covered raw monitor and the plots share the GUI thread. Keep both
  // current with bounded repaint frequency while acquisition runs independently.
  tick.setInterval(qMax(monitorInterval,125));
  connect(timer,&QTimer::timeout,dialog,poll);
  connect(routes,&QComboBox::currentIndexChanged,dialog,select);
  connect(streams,&QComboBox::currentIndexChanged,dialog,poll);
  connect(dialog,&QDialog::finished,this,[this,dialog,timer,monitorInterval]{
    timer->stop();
    dialog->setObjectName({});
    tick.setInterval(monitorInterval);
    backend.acquisition.setCommandPlot({});
  });
  dialog->show();
  if(monitorFocused)focus->click();
  select();timer->start();
}
