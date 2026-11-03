#pragma once
#include "ui_helpers.hpp"
#include "rt_monitor.hpp"
#include <map>

class RtRecordModel : public QStandardItemModel {
  std::string session;
  std::map<unsigned,QList<QStandardItem *>> roots;
  std::map<unsigned,std::map<std::uint64_t,QList<QStandardItem *>>> children;
  std::map<const QStandardItem *,aero::Record> records;
  static QList<QStandardItem *> cells(){QList<QStandardItem *> row;for(int c=0;c<6;++c){auto item=new QStandardItem;item->setEditable(false);row<<item;}return row;}
  void assign(const QList<QStandardItem *> &items,const QStringList &text,const aero::Record &record){
    for(int c=0;c<text.size();++c)if(items[c]->text()!=text[c])items[c]->setText(text[c]);
    records[items[0]]=record;
    const auto p=aero::interpret(record);items[5]->setForeground(QColor(p.valid?"#315f43":"#8a6427"));
    for(auto item:items)item->setToolTip(QString::fromStdString(aero::protocolDiagnostics(record,p)));
  }
  static QString quality(const aero::Record &r){const auto p=aero::interpret(r);if(p.broadcast&&p.valid)return "Broadcast";if(p.valid)return "Responded";
    if((r.vendorStatus&0x200)||p.reason=="Expected status response absent")return "No response";return "Review";}
public:
  using QStandardItemModel::QStandardItemModel;
  const aero::Record *recordFor(const QModelIndex &index) const {
    if(!index.isValid())return nullptr;const auto item=itemFromIndex(index.siblingAtColumn(0));const auto found=records.find(item);return found==records.end()?nullptr:&found->second;
  }
  void update(const aero::RtMonitorFrame &frame,unsigned channel,const QString &busFilter={}) {
    if(session!=frame.session){clear();roots.clear();children.clear();records.clear();session=frame.session;}
    setHorizontalHeaderLabels({"RT / message","Bus A · time / total","Bus B · time / total","Last message","Words (hex)","Response"});
    for(const auto &[rt,row]:frame.channels.at(channel)) {
      auto found=roots.find(rt);
      if(found==roots.end()) {
        auto items=cells();items[0]->setData(rt,Qt::UserRole+1);int position=0;for(const auto &[other,_]:roots)if(other<rt)++position;
        insertRow(position,items);found=roots.emplace(rt,items).first;
      }
      const auto &items=found->second;
      const auto &latest=busFilter=="A"&&row.buses[0].count?row.buses[0].latest:busFilter=="B"&&row.buses[1].count?row.buses[1].latest:row.latest;
      const auto p=aero::interpret(latest);const auto cmd=p.rtToRt&&p.first.rt==rt?p.first:p.payloadCommand;
      auto busText=[](const auto &bus){return bus.count?stamp(bus.latest.timestampNs)+"\n"+QString::number(bus.count)+" messages":QString("— No traffic");};
      assign(items,{rt==32?"Unclassified":rt==31?"Broadcast":QString("RT %1").arg(rt,2,10,QChar('0')),busText(row.buses[0]),busText(row.buses[1]),
        QString("%1 %2 · %3").arg(cmd.mode?"MC":"SA").arg(cmd.mode?cmd.count:cmd.sa,2,10,QChar('0')).arg(cmd.transmit?"Tx":"Rx"),words(latest),quality(latest)},latest);
      if(row.unlistedRouteMessages)items[0]->setToolTip("128 message routes retained per RT. Additional routes remain in the raw recording.");
      for(const auto &[key,route]:row.routes){auto &nodes=children[rt];auto child=nodes.find(key);
        if(child==nodes.end()){auto cellsForRoute=cells();cellsForRoute[0]->setData(int(route.bus),Qt::UserRole+2);items[0]->appendRow(cellsForRoute);child=nodes.emplace(key,cellsForRoute).first;}
        const auto time=stamp(route.latest.timestampNs)+"\n"+QString::number(route.count)+" messages";
        assign(child->second,{QString("%1 %2 · %3").arg(route.mode?"MC":"SA").arg(route.sa,2,10,QChar('0')).arg(route.transmit?"Tx":"Rx"),
          route.bus==0?time:"—",route.bus==1?time:"—",route.bus?"Bus B":"Bus A",words(route.latest),quality(route.latest)},route.latest);
      }
    }
  }
};
