#pragma once
#include "ui_helpers.hpp"
#include "rt_monitor.hpp"
#include <map>

class RtRecordModel : public QStandardItemModel {
public:
  enum class Mode { Statistics, Routes };
private:
  std::string session;
  Mode presentation{Mode::Statistics};
  std::map<unsigned,QList<QStandardItem *>> roots;
  std::map<unsigned,std::map<std::uint64_t,QList<QStandardItem *>>> children;
  std::map<const QStandardItem *,aero::Record> records;
  static QList<QStandardItem *> cells(int columns){QList<QStandardItem *> row;for(int c=0;c<columns;++c){auto item=new QStandardItem;item->setEditable(false);row<<item;}return row;}
  void resetRows(){clear();roots.clear();children.clear();records.clear();session.clear();}
  QList<QStandardItem *> &root(unsigned key,unsigned rt,int bus,int columns){
    auto found=roots.find(key);
    if(found==roots.end()) {
      auto row=cells(columns);row[0]->setData(rt,Qt::UserRole+1);row[0]->setData(bus,Qt::UserRole+2);
      int position=0;for(const auto &[other,_]:roots)if(other<key)++position;
      insertRow(position,row);found=roots.emplace(key,row).first;
    }
    return found->second;
  }
  static QString rtName(unsigned rt){return rt==32?"Unclassified":rt==31?"Broadcast":QString("RT %1").arg(rt,2,10,QChar('0'));}
  void assign(const QList<QStandardItem *> &items,const QStringList &text,const aero::Record &record){
    for(int c=0;c<text.size();++c)if(items[c]->text()!=text[c])items[c]->setText(text[c]);
    records[items[0]]=record;
    const auto p=aero::interpret(record);
    const auto detail=QString("Latest raw record #%1 · CH%2 / Bus %3 · %4\n%5\n%6")
      .arg(record.id).arg(record.channel).arg(record.bus?"B":"A").arg(stamp(record.timestampNs))
      .arg(words(record)).arg(QString::fromStdString(aero::protocolDiagnostics(record,p)));
    for(auto item:items)item->setToolTip(detail);
  }
  static QString quality(const aero::Record &r){const auto p=aero::interpret(r);const auto issues=aero::protocolIssues(r,p);
    if(issues.noResponse)return "No response";if(p.broadcast&&p.valid)return "Broadcast";return p.valid?"Responded":"Review";}
public:
  using QStandardItemModel::QStandardItemModel;
  Mode mode() const{return presentation;}
  void setMode(Mode mode){if(presentation!=mode){presentation=mode;resetRows();}}
  const aero::Record *recordFor(const QModelIndex &index) const {
    if(!index.isValid())return nullptr;const auto item=itemFromIndex(index.siblingAtColumn(0));const auto found=records.find(item);return found==records.end()?nullptr:&found->second;
  }
  void update(const aero::RtMonitorFrame &frame,unsigned channel,const QString &busFilter={}) {
    if(session!=frame.session){resetRows();session=frame.session;}
    if(presentation==Mode::Statistics) {
      setHorizontalHeaderLabels({"RT","Bus","No Response","Illegal Mode","Inv. Data","Inv. Status","Gap in Data","Wrong TADR","BIT in Status","Total"});
      for(const auto &[rt,row]:frame.channels.at(channel))for(unsigned b=0;b<2;++b) {
        const auto &bus=row.buses[b];if(!bus.count)continue;
        auto &items=root(rt*2+b,rt,int(b),10);
        assign(items,{rtName(rt),b?"B":"A",bus.noResponseKnown?QString::number(bus.noResponse):QString("—"),
          QString::number(bus.illegalMode),"—","—","—",QString::number(bus.wrongTadr),"—",QString::number(bus.count)},bus.latest);
        for(int column:{4,5,6,8})items[column]->setToolTip("Unavailable: the admitted SDK/record evidence does not uniquely identify this category. A dash is not zero.\n"+items[column]->toolTip());
        if(!bus.noResponseKnown)items[2]->setToolTip("Unavailable for this source: No Response requires normalized DDC MT/MTI block-status evidence.\n"+items[2]->toolTip());
        items[3]->setToolTip("Reserved mode code, illegal T/R direction, or prohibited broadcast, derived from a present command.\n"+items[3]->toolTip());
        items[7]->setToolTip("A present status word has a terminal address different from its expected responding RT.\n"+items[7]->toolTip());
        items[9]->setToolTip("RT participation count for this bus. RT-to-RT contributes once to each participating RT. Channel Total commands counts each raw message once.\n"+items[9]->toolTip());
        for(int column=2;column<10;++column)items[column]->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
      }
      return;
    }
    setHorizontalHeaderLabels({"RT / message","Bus A · time / total","Bus B · time / total","Last message","Words (hex)","Response"});
    for(const auto &[rt,row]:frame.channels.at(channel)) {
      auto &items=root(rt,rt,-1,6);
      const auto &latest=busFilter=="A"&&row.buses[0].count?row.buses[0].latest:busFilter=="B"&&row.buses[1].count?row.buses[1].latest:row.latest;
      const auto p=aero::interpret(latest);const auto cmd=p.rtToRt&&p.first.rt==rt?p.first:p.payloadCommand;
      auto busText=[](const auto &bus){return bus.count?stamp(bus.latest.timestampNs)+"\n"+QString::number(bus.count)+" messages":QString("— No traffic");};
      assign(items,{rtName(rt),busText(row.buses[0]),busText(row.buses[1]),
        QString("%1 %2 · %3").arg(cmd.mode?"MC":"SA").arg(cmd.mode?cmd.count:cmd.sa,2,10,QChar('0')).arg(cmd.transmit?"Tx":"Rx"),words(latest),quality(latest)},latest);
      if(row.unlistedRouteMessages)items[0]->setToolTip("128 message routes retained per RT. Additional routes remain in the raw recording.");
      for(const auto &[key,route]:row.routes){auto &nodes=children[rt];auto child=nodes.find(key);
        if(child==nodes.end()){auto routeItems=cells(6);routeItems[0]->setData(int(route.bus),Qt::UserRole+2);items[0]->appendRow(routeItems);child=nodes.emplace(key,routeItems).first;}
        const auto time=stamp(route.latest.timestampNs)+"\n"+QString::number(route.count)+" messages";
        assign(child->second,{QString("%1 %2 · %3").arg(route.mode?"MC":"SA").arg(route.sa,2,10,QChar('0')).arg(route.transmit?"Tx":"Rx"),
          route.bus==0?time:"—",route.bus==1?time:"—",route.bus?"Bus B":"Bus A",words(route.latest),quality(route.latest)},route.latest);
      }
    }
  }
};
