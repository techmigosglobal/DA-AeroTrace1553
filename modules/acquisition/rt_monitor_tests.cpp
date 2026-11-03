#include "rt_monitor.hpp"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(){try{
  aero::RtMonitorFrame f;auto r=aero::synthetic(0);f.consume(r);r.id=2;r.words[0]=2000;f.consume(r);
  CHECK(f.channels[0].size()==1&&f.channels[0].at(3).buses[0].count==2&&f.channels[0].at(3).latest.words[0]==2000);
  r.bus=1;r.words[0]=3000;f.consume(r);CHECK(f.channels[0].at(3).buses[0].latest.words[0]==2000&&f.channels[0].at(3).buses[1].latest.words[0]==3000);
  r.channel=2;f.consume(r);CHECK(f.channels[1].at(3).buses[1].count==1&&f.channels[0].at(3).buses[1].count==1);
  r.channel=1;r.commandWord=(2<<11)|(2<<5)|8;f.consume(r);CHECK(f.channels[0].size()==2&&f.channels[0].begin()->first==2);
  r.source=1;r.validity=15;r.messageType=2;r.commandWord=(5<<11)|(2<<5)|1;r.commandWord2=(3<<11)|(1<<10)|(7<<5)|1;r.statusWord=3<<11;r.statusWord2=5<<11;r.words={1200};f.consume(r);
  CHECK(f.channels[0].count(5)&&f.channels[0].at(3).latest.commandWord2==r.commandWord2);
  r.validity=0;f.consume(r);CHECK(f.channels[0].count(32));
  for(unsigned i=0;i<500;++i){r.validity=1;r.commandWord=(3<<11)|(2<<5)|1;r.commandWord2=0;r.messageType=100+i;f.consume(r);}
  CHECK(f.channels[0].at(3).routes.size()==128&&f.channels[0].at(3).unlistedRouteMessages>0);
  CHECK(f.channels[0].at(3).buses[1].count>500);
  aero::RtMonitorFrame modes;auto mode=aero::synthetic(0);mode.commandWord=(3<<11)|(1<<10)|1;modes.consume(mode);mode.commandWord=(3<<11)|(1<<10)|2;modes.consume(mode);
  CHECK(modes.channels[0].at(3).routes.size()==2);
  std::cout<<"PASS: stable RT aggregate, independent channels/buses, RT-to-RT participants, unclassified and bounded routes\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
