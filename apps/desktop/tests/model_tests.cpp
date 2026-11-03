#include "models/record_model.hpp"
#include "models/rt_record_model.hpp"
#include <QAbstractItemModelTester>
#include <QPersistentModelIndex>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(int argc,char **argv){QApplication app(argc,argv);try{
  RecordModel source,shared;shared.sharedSource=&source;shared.channelFilter=1;
  QAbstractItemModelTester a(&source,QAbstractItemModelTester::FailureReportingMode::Fatal);
  QAbstractItemModelTester b(&shared,QAbstractItemModelTester::FailureReportingMode::Fatal);
  for(unsigned cycle=0;cycle<10;++cycle){std::vector<aero::Record> batch;for(unsigned j=0;j<4;++j)batch.push_back(aero::synthetic(cycle*4+j));source.append(batch);shared.syncAppend();
    if(cycle%3==0){source.busFilter=cycle%2?"A":"";source.refresh();shared.refresh();}
    CHECK(source.rows().size()<=5000);for(int row=0;row<shared.rowCount();++row)CHECK(shared.at(row)->channel==1);
  }
  std::vector<aero::Record> large;for(unsigned n=40;n<6040;++n)large.push_back(aero::synthetic(n));source.append(large);shared.syncAppend();CHECK(source.rows().size()==5000);
  source.busFilter.clear();source.refresh();shared.refresh();QPersistentModelIndex selected(source.index(source.rowCount()-1,0));const auto id=source.at(selected.row())->id;
  source.append({aero::synthetic(10000)});shared.syncAppend();CHECK(selected.isValid()&&source.at(selected.row())->id==id);
  CHECK(!source.data(QModelIndex(),Qt::DisplayRole).isValid());CHECK(!source.index(-1,0).isValid());CHECK(!source.index(0,49).isValid());
  RtRecordModel rt;QAbstractItemModelTester c(&rt,QAbstractItemModelTester::FailureReportingMode::Fatal);aero::RtMonitorFrame frame;frame.session="one";
  for(unsigned ch=1;ch<=2;++ch)for(unsigned bus=0;bus<2;++bus)for(unsigned address:{3u,2u}){auto r=aero::synthetic(1);r.channel=ch;r.bus=bus;r.commandWord=(address<<11)|(2<<5)|8;r.statusWord=address<<11;frame.consume(r);}
  rt.update(frame,0);CHECK(rt.rowCount()==2);QPersistentModelIndex stable(rt.index(1,0));
  for(unsigned i=0;i<20;++i){auto r=aero::synthetic(i);r.commandWord=(3<<11)|(2<<5)|8;r.statusWord=3<<11;frame.consume(r);rt.update(frame,0,i%2?"A":"");CHECK(stable.isValid()&&stable.row()==1&&rt.recordFor(stable));}
  frame.session="two";rt.update(frame,1);CHECK(!stable.isValid());CHECK(rt.rowCount()==2);CHECK(!rt.recordFor(QModelIndex()));
  std::cout<<"PASS: source/shared/RT model contracts, append/eviction/filter/reset/persistent indexes\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
