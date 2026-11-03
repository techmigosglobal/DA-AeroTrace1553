#pragma once
#include "ui_helpers.hpp"
#include "protocol.hpp"
class RecordModel : public QAbstractTableModel {
public:
  std::vector<aero::Record> records;
  RecordModel *sharedSource{};
  int lastRemoved{},lastAdded{};
  const std::vector<aero::Record> &rows() const {return sharedSource?sharedSource->records:records;}
  bool matches(const aero::Record &r) const {
    const auto p=aero::interpret(r);
    return (busFilter.isEmpty() || busFilter==(r.bus?"B":"A")) && (rtFilter<0 || (p.commandPresent&&p.first.rt==rtFilter) || (p.command2Present&&p.second.rt==rtFilter)) && (!channelFilter || r.channel==channelFilter) && (search.isEmpty() || words(r).contains(search,Qt::CaseInsensitive) || hexword(r.commandWord).contains(search,Qt::CaseInsensitive));
  }
  void append(const std::vector<aero::Record> &batch) {
    if(batch.empty())return;
    const auto first=batch.size()>5000?batch.size()-5000:0;
    lastAdded=int(batch.size()-first);lastRemoved=qMax(0,int(records.size())+lastAdded-5000);
    int remove=0;while(remove<int(visible.size()) && visible[remove]<lastRemoved)++remove;
    if(remove){beginRemoveRows({},0,remove-1);visible.erase(visible.begin(),visible.begin()+remove);endRemoveRows();}
    if(lastRemoved){records.erase(records.begin(),records.begin()+lastRemoved);deltaNs.erase(deltaNs.begin(),deltaNs.begin()+lastRemoved);clockBackwards.erase(clockBackwards.begin(),clockBackwards.begin()+lastRemoved);for(auto &i:visible)i-=lastRemoved;}
    auto start=records.size();records.insert(records.end(),batch.begin()+first,batch.end());
    deltaNs.resize(records.size());clockBackwards.resize(records.size());
    std::array<int,4> previous{-1,-1,-1,-1};
    for(int i=int(start)-1;i>=0;--i){const auto &r=records[i];if(r.channel>=1&&r.channel<=2&&r.bus<2){auto &v=previous[(r.channel-1)*2+r.bus];if(v<0)v=i;}}
    std::vector<int> add;
    for(int i=int(start);i<int(records.size());++i){const auto &r=records[i];if(r.channel>=1&&r.channel<=2&&r.bus<2){auto &prior=previous[(r.channel-1)*2+r.bus];if(prior>=0){if(r.timestampNs>=records[prior].timestampNs)deltaNs[i]=r.timestampNs-records[prior].timestampNs;else clockBackwards[i]=true;}prior=i;}if(matches(r))add.push_back(i);}
    if(!add.empty()){const int n=visible.size();beginInsertRows({},n,n+int(add.size())-1);visible.insert(visible.end(),add.begin(),add.end());endInsertRows();}
  }
  void syncAppend() {
    if(!sharedSource)return;const int removed=sharedSource->lastRemoved,added=sharedSource->lastAdded;
    int n=0;while(n<int(visible.size())&&visible[n]<removed)++n;
    if(n){beginRemoveRows({},0,n-1);visible.erase(visible.begin(),visible.begin()+n);endRemoveRows();}
    for(auto &v:visible)v-=removed;
    std::vector<int> extra;for(int i=int(rows().size())-added;i<int(rows().size());++i)if(i>=0&&matches(rows()[i]))extra.push_back(i);
    if(!extra.empty()){int at=visible.size();beginInsertRows({},at,at+int(extra.size())-1);visible.insert(visible.end(),extra.begin(),extra.end());endInsertRows();}
  }
  std::vector<int> visible;
  std::vector<std::optional<std::uint64_t>> deltaNs;
  std::vector<bool> clockBackwards;
  QString busFilter, search;
  int rtFilter = -1, channelFilter = 0;
  using QAbstractTableModel::QAbstractTableModel;
  int rowCount(const QModelIndex &p = {}) const override {
    return p.isValid() ? 0 : visible.size();
  }
  int columnCount(const QModelIndex &p = {}) const override {
    return p.isValid() ? 0 : 49;
  }
  QVariant headerData(int s, Qt::Orientation o, int role) const override {
    if (role == Qt::DisplayRole && o == Qt::Horizontal && s>=17) return QString("Word %1 (hex)").arg(s-17);
    if (role == Qt::DisplayRole && o == Qt::Horizontal)
      return QStringList{"#", "Time", "Channel", "Bus", "RT", "SA", "Direction", "WC", "Words (hex)", "Command", "Quality",
                         "Delta (µs)","BSW (hex)","Message type","Command 2","Status 1","Status 2"}
          .value(s);
    return {};
  }
  QVariant data(const QModelIndex &i, int role) const override {
    if (!i.isValid() || i.row() >= visible.size())
      return {};
    const auto &r = rows()[visible[i.row()]];
    const auto &clockBackwards=sharedSource?sharedSource->clockBackwards:this->clockBackwards;
    const auto &deltaNs=sharedSource?sharedSource->deltaNs:this->deltaNs;
    const auto protocol=aero::interpret(r);const auto c=protocol.payloadCommand;
    if(role==Qt::ToolTipRole) return QString("Record %1 · Channel %2 / Bus %3\nCommand %4 / %5 · Status %6 / %7\nVendor BSW %8 · Flags %9 · Message type %10\nDevice ticks %11 · Encoding tick %12 ns; measurement accuracy not established\n%13\n%14\n%15\nExpected/actual payload: %16/%17. Timing verdict: not measured.\nDelta is between retained records on the same channel; skipped display rows may increase it.").arg(r.id).arg(r.channel).arg(r.bus ? "B" : "A").arg(hexword(r.commandWord),hexword(r.commandWord2),hexword(r.statusWord),hexword(r.statusWord2),hexword(r.vendorStatus),hexword(r.flags)).arg(r.messageType).arg(r.deviceTicks).arg(r.tickResolutionNs).arg(words(r),QString::fromStdString(aero::protocolDiagnostics(r,protocol)),QString::fromStdString(aero::statusDescription(protocol))).arg(protocol.expectedWords).arg(protocol.actualWords);
    if (role == Qt::ForegroundRole && i.column() == 10)
      return QColor(clockBackwards[visible[i.row()]] ? "#b3483c" : !protocol.valid ? "#b57200" : "#008663");
    if (role == Qt::FontRole && (i.column() == 8 || i.column() == 9 || i.column()>=12)) {
      QFont f("Consolas");
      f.setPixelSize(13);
      return f;
    }
    if (role != Qt::DisplayRole)
      return {};
    if(i.column()>=17) return i.column()-17<int(r.words.size()) ? QVariant(hexword(r.words[i.column()-17])) : QVariant();
    switch (i.column()) {
    case 0:
      return QVariant::fromValue<qulonglong>(r.id);
    case 1:
      return stamp(r.timestampNs);
    case 2:
      return QString("CH %1").arg(r.channel);
    case 3:
      return r.bus ? "B" : "A";
    case 4:
      return QString("%1").arg(c.rt, 2, 10, QChar('0'));
    case 5:
      return QString("%1").arg(c.sa, 2, 10, QChar('0'));
    case 6:
      return QString::fromUtf8(protocol.direction.data(),qsizetype(protocol.direction.size()));
    case 7:
      if(!protocol.supported||!protocol.commandPresent)return QString("—");
      return protocol.mode ? QVariant(QString("Mode %1").arg(protocol.modeCode)) : QVariant(c.count);
    case 8:
      return words(r);
    case 9:
      return hexword(r.commandWord);
    case 10:
      {const auto quality=aero::protocolQuality(r,protocol,clockBackwards[visible[i.row()]]);return QString::fromUtf8(quality.data(),qsizetype(quality.size()));}
    case 11: {const auto &delta=deltaNs[visible[i.row()]];return delta ? QVariant(double(*delta)/1000.) : QVariant();}
    case 12: return hexword(r.vendorStatus);
    case 13: return r.messageType;
    case 14: return hexword(r.commandWord2);
    case 15: return hexword(r.statusWord);
    case 16: return hexword(r.statusWord2);
    }
    return {};
  }
  void refresh() {
    beginResetModel();
    const auto &records=rows();
    visible.clear();
    deltaNs.assign(records.size(),{});clockBackwards.assign(records.size(),false);std::array<int,4> previous{-1,-1,-1,-1};
    for (int i = 0; i < records.size(); ++i) {
      auto &r = records[i];
      auto c = aero::command(r.commandWord);
      if(r.channel>=1 && r.channel<=2 && r.bus<2) {
        auto &prior=previous[(r.channel-1)*2+r.bus];
        if(prior>=0 && r.timestampNs>=records[prior].timestampNs) deltaNs[i]=r.timestampNs-records[prior].timestampNs;
        else if(prior>=0) clockBackwards[i]=true;
        prior=i;
      }
      if (!busFilter.isEmpty() && busFilter != (r.bus ? "B" : "A"))
        continue;
      const auto protocol=aero::interpret(r);
      if (rtFilter >= 0 && (!protocol.commandPresent||protocol.first.rt!=rtFilter) && (!protocol.command2Present||protocol.second.rt!=rtFilter))
        continue;
      if (channelFilter && r.channel != channelFilter)
        continue;
      if (!search.isEmpty() &&
          !words(r).contains(search, Qt::CaseInsensitive) &&
          !hexword(r.commandWord).contains(search, Qt::CaseInsensitive))
        continue;
      visible.push_back(i);
    }
    endResetModel();
  }
  const aero::Record *at(int row) const {
    return row >= 0 && row < visible.size() && visible[row]>=0 && visible[row]<rows().size() ? &rows()[visible[row]] : nullptr;
  }
};

