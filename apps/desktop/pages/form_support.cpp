#include "main_window.hpp"

void Window::populate(QTableWidget *table, const QList<QStringList> &rows) {
  table->setRowCount(rows.size());
  table->verticalHeader()->hide();
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table->verticalHeader()->setDefaultSectionSize(41);
  for(int row=0;row<rows.size();++row)
    for(int column=0;column<table->columnCount();++column)
      table->setItem(row,column,new QTableWidgetItem(rows[row].value(column)));
  if(!rows.empty()) table->selectRow(0);
}

QList<QStringList> Window::messageRows(const std::vector<aero::Record> &records, int limit) {
  QList<QStringList> rows;
  for(int i=0;i<std::min<int>(limit,records.size());++i) {
    const auto &record=records[i]; const auto command=aero::command(record.commandWord);
    rows << QStringList{stamp(record.timestampNs),record.bus ? "B" : "A",QString::number(command.rt),QString::number(command.sa),command.transmit ? "RT→BC" : "BC→RT",words(record)};
  }
  return rows;
}

void Window::registerPlot(Plot *plot, const QString &role, int signalIndex) {
  plot->setTimeAxis(role!="distribution");
  plot->setProperty("role",role); plot->setProperty("signalIndex",signalIndex);
  plot->setProperty("sourceChannel",role.isEmpty() || role=="raw_column" ? analysisChannel : 0);plot->setProperty("sourceBus",role.isEmpty() || role=="raw_column" ? analysisBus : -1);plot->setProperty("rawColumn",-1);
  plots << plot;
  if(role.isEmpty()) {std::vector<unsigned> selected;for(auto view:plots) if(view->property("role").toString().isEmpty()) selected.push_back(view->property("signalIndex").toUInt());backend.acquisition.setTraceSignals(std::move(selected));}
}

void Window::configureCursor(Plot *plot) {
  plot->onPick=[this,plot](QPointF point) {
    if(!cursorALabel) return;
    const auto sample=plot->nearestSample(point.x());if(!sample) return;point=*sample;
    if(!cursorAValue) {
      cursorAValue=point;
      cursorALabel->setText(QString("A: %1 · %2").arg(stamp(std::uint64_t(std::round(std::max(0.,point.x())*1e9)))).arg(point.y(),0,'g',12));
      cursorBLabel->setText("B: click another point");
    } else {
      const auto first=*cursorAValue;
      cursorBLabel->setText(QString("B: %1 · %2").arg(stamp(std::uint64_t(std::round(std::max(0.,point.x())*1e9)))).arg(point.y(),0,'g',12));
      cursorDeltaLabel->setText(QString("Δt: %1 µs    ΔY: %2").arg((point.x()-first.x())*1e6,0,'f',3).arg(point.y()-first.y(),0,'g',12));
      cursorAValue.reset();
    }
    const auto &data=analysisData(); const auto end=analysisEnd();
    const auto mappings=backend.mappings();const auto signal=plot->property("signalIndex").toUInt();
    double distance=std::numeric_limits<double>::infinity();
    backend.selected={};
    for(auto i=backend.replaySource && end>5000 ? end-5000 : 0;i<end;++i) {
      const auto &record=data[i];
      if(plot->property("sourceChannel").toUInt() && record.channel!=plot->property("sourceChannel").toUInt()) continue;
      if(plot->property("sourceBus").toInt()>=0 && record.bus!=plot->property("sourceBus").toInt()) continue;
      if(plot->property("role").toString().isEmpty() && (signal>=mappings.size() || !aero::decode(record,mappings[signal]))) continue;
      const auto delta=std::abs(record.timestampNs/1e9-point.x());if(delta<distance) {distance=delta;backend.selected=record;}
    }
    if(distance>0.0000005) {backend.selected={};toast("That plotted record is outside the retained analysis window. Open its complete session to inspect the raw words.");}
  };
}
