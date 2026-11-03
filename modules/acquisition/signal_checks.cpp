#include "signal_checks.hpp"
#include "protocol.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace aero {
std::string validateChecks(const std::vector<CheckRule> &rules,const std::vector<Signal> &profile) {
  if(rules.size()>512) return "Maximum 512 signal checks";
  std::set<std::string> ids;
  for(const auto &r:rules) {
    if(!ids.insert(r.signalId).second) return "Duplicate signal check: "+r.signalId;
    if(std::none_of(profile.begin(),profile.end(),[&](const auto &s){return s.id==r.signalId;})) return "Check refers to unknown signal: "+r.signalId;
    if((r.minimum && !std::isfinite(*r.minimum)) || (r.maximum && !std::isfinite(*r.maximum)) || !std::isfinite(r.hysteresis) || r.hysteresis<0 ||
       (r.minimum && r.maximum && (*r.minimum>*r.maximum || r.hysteresis>(*r.maximum-*r.minimum)/2))) return "Invalid limits/hysteresis: "+r.signalId;
    if(r.missingNs>3600000000000ULL) return "Missing-signal timeout must be at most one hour";
  }
  return {};
}
void SignalChecks::configure(const std::vector<Signal> &profile,const std::vector<CheckRule> &value) {
  if(auto error=validateChecks(value,profile);!error.empty())throw std::invalid_argument(error);
  rules=value;mappings.clear();states.assign(rules.size(),{});streams={};pending.clear();
  for(const auto &r:rules) {
    const auto s=std::find_if(profile.begin(),profile.end(),[&](const auto &s){return s.id==r.signalId;});
    if(s==profile.end()) throw std::invalid_argument("Unknown check signal");mappings.push_back(*s);
  }
}
void SignalChecks::event(std::uint64_t ns,unsigned stream,const Record *r,const std::string &id,const std::string &kind,const std::string &detail) {
  // A consumer drains after every record/advance; cap also protects misuse.
  if(pending.size()<4096) pending.push_back({ns,r ? r->id : 0,stream/2+1,int(stream%2),id,kind,detail});
}
void SignalChecks::consume(const Record &r) {
  if(r.channel<1 || r.channel>2 || r.bus>1) return;
  const auto k=(r.channel-1)*2+r.bus;auto &stream=streams[k];
  if(stream.seen && r.timestampNs<stream.now) {
    event(r.timestampNs,k,&r,"","clock_reset","Card time moved backwards; signal timers restarted");
    for(auto &s:states) s[k]={};stream={};
  }
  if(!stream.seen) {stream.seen=true;stream.first=r.timestampNs;}
  stream.now=r.timestampNs;
  // Test the elapsed gap before an arriving value resets its signal timer.
  advance(k,r.timestampNs);
  const auto interpretation=interpret(r);
  const bool protocol=!interpretation.valid,status=interpretation.statuses[0].fault() || interpretation.statuses[1].fault() || interpretation.statuses[0].busy() || interpretation.statuses[1].busy();
  if(protocol && !stream.protocol) event(r.timestampNs,k,&r,"","protocol",protocolDiagnostics(r,interpretation));
  if(status && !stream.status) event(r.timestampNs,k,&r,"","status",statusDescription(interpretation));
  stream.protocol=protocol;stream.status=status;
  for(std::size_t i=0;i<rules.size();++i) {
    const auto &signal=mappings[i];if((signal.channel && signal.channel!=r.channel) || (signal.bus>=0 && signal.bus!=r.bus)) continue;
    const auto c=interpretation.payloadCommand;if(c.rt!=signal.rt || c.sa!=signal.sa || c.transmit!=signal.transmit) continue;
    const auto v=decode(r,signal);if(!v) continue;
    auto &s=states[i][k];const auto &rule=rules[i];
    if(s.missing) event(r.timestampNs,k,&r,signal.id,"recovered","Signal received again");
    s.last=r.timestampNs;s.seen=true;s.missing=false;
    const bool outside=(rule.minimum && *v<*rule.minimum) || (rule.maximum && *v>*rule.maximum);
    const bool inside=(!rule.minimum || *v>=*rule.minimum+rule.hysteresis) && (!rule.maximum || *v<=*rule.maximum-rule.hysteresis);
    if(outside && !s.range) {event(r.timestampNs,k,&r,signal.id,"range","Engineering value "+std::to_string(*v)+" "+signal.unit);s.range=true;}
    else if(s.range && inside) {event(r.timestampNs,k,&r,signal.id,"range_recovered","Signal returned within limits");s.range=false;}
  }
  advance(k,r.timestampNs);
}
void SignalChecks::advance(unsigned k,std::uint64_t ns) {
  if(k>=4 || !streams[k].seen || ns<streams[k].now) return;
  for(std::size_t i=0;i<rules.size();++i) {
    const auto &signal=mappings[i];const auto &rule=rules[i];auto &s=states[i][k];
    if(!rule.missingNs || (signal.channel && signal.channel!=k/2+1) || (signal.bus>=0 && signal.bus!=int(k%2))) continue;
    const auto last=s.seen ? s.last : streams[k].first;
    if(!s.missing && ns>=last && ns-last>rule.missingNs) {event(ns,k,nullptr,signal.id,"missing","No valid signal within configured timeout");s.missing=true;}
  }
}
std::vector<CheckEvent> SignalChecks::drain(){auto result=std::move(pending);pending.clear();return result;}
}

