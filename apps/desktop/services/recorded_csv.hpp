#pragma once

#include "csv_conversion.hpp"
#include "value_display.hpp"
#include "protocol.hpp"
#include <array>
#include <limits>

namespace desktop {
struct CsvTrace {
  QString title;
  int word{-1};
  std::optional<aero::Signal> mapping;
  std::optional<double> lower,upper;
};
struct RecordedCsvRequest {
  QString path;
  int radix{16};
  unsigned channel{1},bus{};
  aero::Command route;
  std::optional<std::uint64_t> first,last;
  std::vector<CsvTrace> traces;
};
struct RecordedCsvPoint {QPointF point;quint64 row{};};
struct RecordedCsvStats {
  quint64 count{},gaps{},boundaries{},excursions{};
  long double mean{},m2{},squares{};
  double minimum{},maximum{};
  void add(double value) {
    if(!count){minimum=maximum=value;}
    else{minimum=std::min(minimum,value);maximum=std::max(maximum,value);}
    const auto v=static_cast<long double>(value);
    const auto delta=v-mean;mean+=delta/++count;m2+=delta*(v-mean);squares+=v*v;
  }
  QString text() const {
    if(!count)return "No valid samples in the scanned range.";
    return QString("Scanned range · %1 samples · %2 gaps · %3 clock jumps · %4 excursions\nMin %5 · Max %6 · Mean %7\nσ %8 · RMS %9 · Peak-to-peak %10")
      .arg(count).arg(gaps).arg(boundaries).arg(excursions)
      .arg(displayValue(minimum),displayValue(maximum),displayValue(double(mean)),displayValue(double(std::sqrt(std::max(0.L,m2/count)))),displayValue(double(std::sqrt(squares/count))),displayValue(maximum-minimum));
  }
};
struct RecordedCsvSeries {QVector<RecordedCsvPoint> points;RecordedCsvStats statistics;};
struct RecordedCsvResult {QString error;bool cancelled{},omittedSegments{};std::vector<RecordedCsvSeries> series;};

inline RecordedCsvResult readRecordedCsv(const RecordedCsvRequest &request,const std::atomic_bool &cancel) {
  RecordedCsvResult result;quint64 row=0;
  try{
    QFile file(request.path);
    if(!file.open(QIODevice::ReadOnly))throw std::runtime_error(file.errorString().toStdString());
    const QFileInfo identity(request.path);
    const auto inputBytes=identity.size();const auto inputModified=identity.lastModified();
    const CsvInput csv(file);
    if(request.traces.empty()||request.traces.size()>4)throw std::runtime_error("Select one to four traces");
    if(request.channel<1||request.channel>2||request.bus>1)throw std::runtime_error("Select one source channel and bus");
    const auto dataStart=file.pos();qint64 envelopeStart=dataStart,envelopeBytes=inputBytes-dataStart;
    if(request.first||request.last){
      // Locate the selected range before allocating display buckets. Otherwise
      // zooming into a small part of a large file would still retain only the
      // few buckets allotted to that part of the original file.
      qint64 first=-1,last=dataStart;
      while(!file.atEnd()){
        if(cancel.load()){result.cancelled=true;return result;}
        const auto offset=file.pos();const auto bytes=csvRow(file);if(bytes.trimmed().isEmpty())continue;
        ++row;const auto cells=csvCells(bytes);csv.validate(cells);const auto time=csvTime(cells[0],csv.compact);
        if(cells[4]!=(request.bus?"B":"A")||(request.first&&time<*request.first)||(request.last&&time>*request.last))continue;
        if(first<0)first=offset;last=file.pos();
      }
      if(first>=0){envelopeStart=first;envelopeBytes=last-first;}
      if(!file.seek(dataStart))throw std::runtime_error("Cannot rewind CSV for range analysis");
      row=0;
    }
    result.series.resize(request.traces.size());
    struct Bucket {
      qint64 slot{-1};
      std::optional<RecordedCsvPoint> first,minimum,maximum,last;
      std::optional<std::uint64_t> previous;
      bool gap{},omit{};
    };
    std::array<Bucket,4> buckets;
    constexpr qsizetype budget=16384;
    const auto flush=[&](size_t i) {
      auto &b=buckets[i];auto &out=result.series[i].points;
      if(!b.first)return;
      std::array<RecordedCsvPoint,4> keep{*b.first,*b.minimum,*b.maximum,*b.last};
      std::sort(keep.begin(),keep.end(),[](auto a,auto c){return a.row<c.row;});
      for(const auto &point:keep)if(out.isEmpty()||out.back().row!=point.row){
        if(out.size()>=budget){b.omit=true;result.omittedSegments=true;break;}
        out.append(point);
      }
      b.first.reset();b.minimum.reset();b.maximum.reset();b.last.reset();
    };
    const auto breakTrace=[&](size_t i,double seconds) {
      auto &b=buckets[i];auto &series=result.series[i];
      flush(i);
      if(!b.gap) {
        ++series.statistics.gaps;b.gap=true;
        if(!b.omit&&series.points.size()<budget)series.points.append({{seconds,std::numeric_limits<double>::quiet_NaN()},0});
      }
    };
    while(!file.atEnd()){
      if(cancel.load()){result.cancelled=true;return result;}
      const auto offset=file.pos();const auto bytes=csvRow(file);
      if(bytes.trimmed().isEmpty())continue;
      ++row;const auto cells=csvCells(bytes);csv.validate(cells);
      if(cells[4]!=(request.bus?"B":"A"))continue;
      const auto record=csv.record(cells,request.radix,request.channel,row);
      const auto protocol=aero::interpret(record);
      const auto &command=protocol.payloadCommand;
      if(command.rt!=request.route.rt||command.sa!=request.route.sa||command.transmit!=request.route.transmit||command.mode!=request.route.mode)continue;
      if((request.first&&record.timestampNs<*request.first)||(request.last&&record.timestampNs>*request.last))continue;
      const auto x=record.timestampNs/1e9;
      const auto slot=qint64((static_cast<long double>(offset-envelopeStart)*2048)/std::max<qint64>(1,envelopeBytes));
      for(size_t i=0;i<request.traces.size();++i){
        auto &series=result.series[i];auto &b=buckets[i];const auto &trace=request.traces[i];
        if(b.previous&&record.timestampNs<*b.previous){++series.statistics.boundaries;breakTrace(i,x);}
        b.previous=record.timestampNs;
        std::optional<double> value;
        if(trace.mapping)value=aero::decode(record,*trace.mapping,false);
        else if(trace.word>=0&&size_t(trace.word)<record.words.size())value=record.words[trace.word];
        if(!value||!std::isfinite(*value)){breakTrace(i,x);continue;}
        series.statistics.add(*value);
        if((trace.lower&&*value<*trace.lower)||(trace.upper&&*value>*trace.upper))++series.statistics.excursions;
        if(b.omit)continue;
        if(b.slot!=slot||b.gap)flush(i);
        b.slot=slot;b.gap=false;
        const RecordedCsvPoint point{{x,*value},row};
        if(!b.first)b.first=b.minimum=b.maximum=point;
        if(*value<b.minimum->point.y())b.minimum=point;
        if(*value>b.maximum->point.y())b.maximum=point;
        b.last=point;
      }
    }
    for(size_t i=0;i<result.series.size();++i)flush(i);
    const QFileInfo after(request.path);
    if(after.size()!=inputBytes||after.lastModified()!=inputModified)throw std::runtime_error("CSV changed while reading; reopen the file");
  }catch(const std::exception &e){result.error=QString("CSV row %1: %2").arg(row).arg(QString::fromUtf8(e.what()));}
  return result;
}
}
