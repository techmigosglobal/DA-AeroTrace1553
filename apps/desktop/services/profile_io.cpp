#include "profile_io.hpp"
#include "live_decoder.hpp"
#include <QJsonArray>
#include <cmath>
namespace desktop {
QJsonObject signalToJson(const aero::Signal &s) {
  return {{"schema","aerotrace.icd.v1"},{"id",QString::fromStdString(s.id)},{"unit",QString::fromStdString(s.unit)},
    {"rt",int(s.rt)},{"sa",int(s.sa)},{"word",int(s.word)},{"lsb",int(s.lsb)},{"width",int(s.width)},
    {"scale",s.scale},{"offset",s.offset},{"signed",s.signedValue},{"transmit",s.transmit},
    {"channel",int(s.channel)},{"bus",s.bus},{"numeric_type",QString::fromStdString(s.numericType)},
    {"lsw_first",s.leastSignificantWordFirst},{"expected_word_count",int(s.expectedWordCount)},{"simulation_only",s.simulationOnly},{"source_page",int(s.sourcePage)},
    {"source_document",QString::fromStdString(s.sourceDocument)},{"source_hash",QString::fromStdString(s.sourceHash)},{"operator_note",QString::fromStdString(s.operatorNote)},
    {"subsystem",QString::fromStdString(s.subsystem)},{"message_id",QString::fromStdString(s.messageId)},{"source_signal_id",QString::fromStdString(s.sourceSignalId)},{"icd_set_id",QString::fromStdString(s.icdSetId)},{"icd_revision",QString::fromStdString(s.icdRevision)}};
}
QString signalFromJson(const QJsonObject &o,aero::Signal &s) {
  for(const QString &key:{"rt","sa","word","lsb","width","scale","offset"}) {
    if(!o[key].isDouble() || !std::isfinite(o[key].toDouble())) return "Missing/invalid numeric field: "+key;
    const auto value=o[key].toDouble();
    if(key!="scale" && key!="offset" && (value<0 || value>64 || value!=std::floor(value))) return "Invalid integer field: "+key;
  }
  if(!o["id"].isString() || !o["unit"].isString() || !o["signed"].isBool() || !o["transmit"].isBool())
    return "ID, unit, signedness and direction must be explicit";
  s.id=o["id"].toString().toStdString();s.unit=o["unit"].toString().toStdString();
  s.rt=o["rt"].toInt();s.sa=o["sa"].toInt();s.word=o["word"].toInt();s.lsb=o["lsb"].toInt();s.width=o["width"].toInt();
  s.scale=o["scale"].toDouble();s.offset=o["offset"].toDouble();s.signedValue=o["signed"].toBool();s.transmit=o["transmit"].toBool();
  if(o.contains("channel")) {const auto value=o["channel"].toDouble(-99);if(value<0 || value>2 || value!=std::floor(value)) return "Invalid channel";s.channel=int(value);}
  if(o.contains("bus")) {const auto value=o["bus"].toDouble(-99);if(value < -1 || value>1 || value!=std::floor(value)) return "Invalid bus";s.bus=int(value);}
  if(o.contains("expected_word_count")){const auto value=o["expected_word_count"].toDouble(-99);if(value<0 || value>32 || value!=std::floor(value))return "Invalid expected word count";s.expectedWordCount=unsigned(value);}
  if(o.contains("simulation_only")&&!o["simulation_only"].isBool())return "Invalid simulation-only flag";
  s.simulationOnly=o.value("simulation_only").toBool(false);
  s.numericType=o["numeric_type"].toString("integer").toStdString();s.leastSignificantWordFirst=o["lsw_first"].toBool(false);
  s.sourcePage=o["source_page"].toInt();s.sourceDocument=o["source_document"].toString().toStdString();s.sourceHash=o["source_hash"].toString().toStdString();
  s.icdSetId=o["icd_set_id"].toString().toStdString();s.icdRevision=o["icd_revision"].toString().toStdString();
  s.subsystem=o["subsystem"].toString().toStdString();
  s.messageId=o["message_id"].toString().toStdString();
  s.sourceSignalId=o["source_signal_id"].toString().toStdString();
  {
    const auto parts=QString::fromStdString(s.id).split(" / ");
    if(parts.size()==3 && parts[0].startsWith("Subsystem-") && !parts[1].isEmpty() && !parts[2].isEmpty()) {
      if(s.subsystem.empty())s.subsystem=parts[0].toStdString();
      if(s.messageId.empty())s.messageId=parts[1].toStdString();
      if(s.sourceSignalId.empty())s.sourceSignalId=parts[2].toStdString();
    }
  }
  if(s.sourceHash==AEROTRACE_OFFLINE_ICD_SHA256) {
    s.simulationOnly=true;
    s.operatorNote="Offline simulation fixture";
  } else
    s.operatorNote=o["operator_note"].toString().left(2000).toStdString();
  return QString::fromStdString(aero::validate(s));
}
QJsonObject profileToJson(const std::vector<aero::Signal> &mappings) {
  QJsonArray values;for(const auto &signal:mappings) values.append(signalToJson(signal));
  return {{"schema","aerotrace.icd.profile.v2"},{"signals",values}};
}
QString profileFromJson(const QJsonObject &o,std::vector<aero::Signal> &mappings) {
  mappings.clear();
  if(o["draft"].toBool()) return "Draft profiles must be reviewed and published before activation";
  if(o["schema"].toString()=="aerotrace.icd.v1") {
    aero::Signal signal;if(auto error=signalFromJson(o,signal);!error.isEmpty()) return error;mappings.push_back(signal);
  } else if(o["schema"].toString()=="aerotrace.icd.profile.v2" && o["signals"].isArray()) {
    if(o["signals"].toArray().size()>512) return "Maximum 512 active signals";
    for(const auto &value:o["signals"].toArray()) {
      if(!value.isObject()) return "Invalid signal object";
      aero::Signal signal;if(auto error=signalFromJson(value.toObject(),signal);!error.isEmpty()) return error;mappings.push_back(signal);
    }
  } else return "Unsupported ICD schema";
  return QString::fromStdString(aero::validateProfile(mappings));
}
}
