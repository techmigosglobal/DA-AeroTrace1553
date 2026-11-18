#include "main_window.hpp"

void Window::configureAnalysisTools(bool recorded) {
  auto tools=new QWidget(content);tools->setObjectName("analysisToolbar");auto row=new QHBoxLayout(tools);row->setContentsMargins(0,0,0,0);row->setSpacing(5);
  auto body=content->findChild<QVBoxLayout *>("bodyLayout");
  auto actionBar=content->findChild<QWidget *>("actionBar");
  if(!actionBar)actionBar=titleActions?titleActions->parentWidget()->findChild<QWidget *>("actionBar"):nullptr;
  if(actionBar){auto actions=qobject_cast<QHBoxLayout *>(actionBar->layout());if(actions)actions->addWidget(tools);}
  auto menu=new QMenu(tools);auto menuButton=new QToolButton(tools);menuButton->setText("Tools");menuButton->setPopupMode(QToolButton::InstantPopup);menuButton->setMenu(menu);row->addWidget(menuButton);
  const auto available=plots;
  const auto action=[menu,tools](const QString &text,const QString &help,std::function<void()> callback){auto item=menu->addAction(text);item->setToolTip(help);QObject::connect(item,&QAction::triggered,tools,std::move(callback));return item;};
  auto modes=new QActionGroup(tools);modes->setExclusive(true);
  for(int i=0;i<3;++i){auto button=action(QStringList{"Cursor","Pan","Zoom"}[i],QStringList{"Inspect samples and place A/B cursors","Drag to pan the selected trace","Drag a rectangle to zoom"}[i],[available,i]{for(auto plot:available){
#ifdef AEROTRACE_QWT
    if(i>0)if(auto graph=plot->findChild<QwtPlot *>()){auto zoomer=plot->findChild<QwtPlotZoomer *>();if(plot->followsLive()||i==1||!zoomer||zoomer->zoomRectIndex()==0){const auto x=graph->axisScaleDiv(QwtPlot::xBottom),y=graph->axisScaleDiv(QwtPlot::yLeft);plot->setAxes(x.lowerBound(),x.upperBound(),y.lowerBound(),y.upperBound());if(zoomer)zoomer->setZoomBase();}}
#endif
    plot->setInteraction(i);
  }});button->setCheckable(true);button->setChecked(i==0);modes->addAction(button);}
  action("Back","Previous zoom",[available]{for(auto plot:available)if(plot->isVisible())plot->zoomBack();});
  action(recorded?"Reset view":"Follow",recorded?"Reset axes for the recorded range":"Reset axes and follow incoming samples",[available]{for(auto plot:available)plot->resetView();});
  action("Axes / range","Set a card-time range and optional engineering limits",[this,available]{
    auto dialog=new QDialog(content);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("Plot axes and time range");auto layout=new QFormLayout(dialog);
    auto x0=new QLineEdit("0"),x1=new QLineEdit("10"),y0=new QLineEdit,y1=new QLineEdit;layout->addRow("Start (card seconds)",x0);layout->addRow("End (card seconds)",x1);layout->addRow("Y minimum (blank = auto)",y0);layout->addRow("Y maximum (blank = auto)",y1);
    auto notice=new QLabel("Axes affect the view only. Limits and excursions use the saved signal-alert rules.");notice->setWordWrap(true);layout->addRow(notice);
    auto error=new QLabel;layout->addRow(error);auto buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Close);layout->addRow(buttons);
    connect(buttons->button(QDialogButtonBox::Close),&QPushButton::clicked,dialog,&QDialog::close);
    connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,dialog,[available,x0,x1,y0,y1,error]{bool a,b,c=true,d=true;const auto lo=x0->text().toDouble(&a),hi=x1->text().toDouble(&b);std::optional<double> low,high;if(!y0->text().trimmed().isEmpty())low=y0->text().toDouble(&c);if(!y1->text().trimmed().isEmpty())high=y1->text().toDouble(&d);if(!a||!b||!c||!d||lo<0||hi<=lo||!std::isfinite(hi)||low.has_value()!=high.has_value()||(low&&(!std::isfinite(*low)||!std::isfinite(*high)||*high<=*low))){error->setText("Enter an increasing finite X range and either both Y bounds or neither.");return;}error->clear();for(auto plot:available)if(plot->isVisible())plot->setAxes(lo,hi,low,high);});dialog->resize(420,280);dialog->show();
  });
  if(!recorded)action("Limits","Configure signal alerts and limit lines",[this]{configureSignalAlerts();});
  auto stats=new QLabel(content);stats->setObjectName("rangeStatistics");stats->setWordWrap(false);stats->setFixedHeight(72);stats->hide();if(body&&!recorded)body->addWidget(stats);
  auto statsButton=action("Statistics","Show statistics for the visible range",[stats]{stats->setVisible(!stats->isVisible());});statsButton->setCheckable(true);
  statsButton->setVisible(!recorded);
  stats->setProperty("traceIndex",0);
  if(!available.empty()&&!recorded)available.front()->showStatistics(stats);
  for(auto plot:available){auto pick=plot->onPick;plot->onPick=[available,plot,pick,stats,recorded](QPointF point){stats->setProperty("traceIndex",available.indexOf(plot));if(!recorded){for(auto other:available)other->showStatistics(nullptr);plot->showStatistics(stats);}if(pick)pick(point);};}
  auto focus=action("Focus plot","Hide surrounding navigation and subsystem controls",[]{});focus->setObjectName("focusPlot");focus->setCheckable(true);
  connect(focus,&QAction::toggled,content,[this](bool on){sidebar->setVisible(!on);if(auto workspace=scroll->widget()){if(auto panel=workspace->findChild<QWidget *>("subsystemPanel"))panel->setVisible(!on);if(auto tabs=workspace->findChild<QTabBar *>("workspaceTabs"))tabs->setVisible(!on);if(auto scope=workspace->findChild<QLabel *>("workspaceScope"))scope->setVisible(!on);}});
  connect(content,&QObject::destroyed,sidebar,[side=QPointer<QFrame>(sidebar)]{if(side)side->show();});
  action(recorded?"Envelope CSV":"Data CSV",recorded?"Export plotted envelope points":"Export retained samples from the selected trace",[this,available,stats]{
    Plot *plot=available.value(stats->property("traceIndex").toInt());if(!plot||!plot->isVisible())plot=available.value(0);if(!plot)return;
    auto path=QFileDialog::getSaveFileName(this,"Export retained plot samples",sessionDir+"/trace.csv","CSV (*.csv)");if(path.isEmpty())return;
    if(QFileInfo::exists(path)){toast("Choose a new filename; existing exports are preserved.");return;}QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)){toast(file.errorString());return;}
    file.write("card_seconds,engineering_value,quality\n");for(const auto &p:plot->points()){const auto line=QString("%1,%2,%3\n").arg(p.x(),0,'g',17).arg(std::isfinite(p.y())?QString::number(p.y(),'g',17):QString(),std::isfinite(p.y())?"decoded":"gap").toUtf8();if(file.write(line)!=line.size()){toast(file.errorString());return;}}if(file.commit())toast("Retained plot data exported: "+path);else toast(file.errorString());
  });
  row->addStretch();
}
