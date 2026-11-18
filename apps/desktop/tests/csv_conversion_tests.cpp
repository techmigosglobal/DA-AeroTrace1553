#include "services/csv_conversion.hpp"
#include "services/recorded_csv.hpp"
#include <iostream>

#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL line "<<__LINE__<<": " #x "\n";return 1;}}while(false)
int main(int argc,char **argv) {
  QCoreApplication app(argc,argv);QTemporaryDir temporary;
  if(app.arguments().contains("--scan-fixture")){
    const auto path=app.arguments().value(app.arguments().indexOf("--scan-fixture")+1);
    QFile source(path);CHECK(source.open(QIODevice::ReadOnly));desktop::CsvInput csv(source);
    desktop::RecordedCsvRequest request;request.path=path;request.radix=16;
    bool selected=false;quint64 rows=0;QCryptographicHash before(QCryptographicHash::Sha256);
    const auto argument=app.arguments().indexOf("--scan-fixture");
    if(!app.arguments().value(argument+2).isEmpty()){request.route=aero::command(desktop::csvWord(app.arguments()[argument+2],16));request.traces={{"Raw word 0",0}};selected=true;}
    request.channel=app.arguments().value(argument+3,"1").toUInt();CHECK(request.channel==1||request.channel==2);
    CHECK(source.seek(0)&&before.addData(&source));const auto original=before.result();CHECK(source.seek(0));desktop::CsvInput header(source);
    while(!source.atEnd()){
      const auto bytes=desktop::csvRow(source);if(bytes.trimmed().isEmpty())continue;
      const auto cells=desktop::csvCells(bytes);const auto record=csv.record(cells,16,1,++rows);const auto protocol=aero::interpret(record);
      if(!selected&&!protocol.mode&&!record.words.empty()){selected=true;request.route=protocol.payloadCommand;request.bus=record.bus;request.traces={{"Raw word 0",0}};}
    }
    source.close();CHECK(selected);std::atomic_bool stop{false};const auto result=desktop::readRecordedCsv(request,stop);CHECK(result.error.isEmpty()&&!result.series.empty()&&result.series[0].statistics.count);
    CHECK(source.open(QIODevice::ReadOnly));QCryptographicHash after(QCryptographicHash::Sha256);CHECK(after.addData(&source)&&after.result()==original);
    const auto &series=result.series[0];std::cout<<"PASS: "<<QFileInfo(path).fileName().toStdString()<<"; layout="<<(csv.compact?11:42)<<"; rows="<<rows<<"; matching="<<series.statistics.count<<"; backward="<<series.statistics.boundaries<<"; plotted="<<series.points.size()<<"; source SHA-256="<<original.toHex().toStdString()<<"\n";
    return 0;
  }
  aero::Record r;r.source=1;r.validity=5;r.messageType=0;r.channel=1;r.timestampNs=48745527000ULL;
  r.commandWord=0x0947;r.statusWord=0x0800;r.vendorStatus=0x8100;r.words={0xaaaa,0x5042,1,3,0x14aa,5,7};
  if(app.arguments().contains("--benchmark")){
    auto sample=r;sample.commandWord=0x2440;sample.messageType=1;sample.words.assign(32,0xffff);
    const auto row=QByteArray::fromStdString(aero::sessionCsvRecord(sample)),header=QByteArray::fromStdString(aero::sessionCsvHeader());
    const auto path=temporary.filePath("benchmark.csv");QFile file(path);CHECK(file.open(QIODevice::WriteOnly));file.write(header);file.write(row.repeated(10000));file.close();
    QElapsedTimer timer;
    for(const auto chunk:{4096,1024*1024}){CHECK(file.open(QIODevice::ReadOnly));timer.start();while(!file.atEnd())file.readLine(chunk+1);std::cout<<"readLine "<<chunk<<": "<<timer.elapsed()<<" ms\n"<<std::flush;file.close();}
    CHECK(file.open(QIODevice::ReadOnly));desktop::CsvInput format(file);timer.start();while(!file.atEnd())desktop::csvCells(desktop::csvRow(file));std::cout<<"Read + parse: "<<timer.elapsed()<<" ms\n"<<std::flush;file.close();
    const auto cells=desktop::csvCells(row);timer.start();for(int n=0;n<10000;++n)format.convert(cells,16,aero::NumericRadix::Decimal);std::cout<<"Convert: "<<timer.elapsed()<<" ms\n"<<std::flush;
    QSaveFile output(temporary.filePath("output.csv"));CHECK(output.open(QIODevice::WriteOnly));timer.start();for(int n=0;n<10000;++n)output.write(row);CHECK(output.commit());std::cout<<"Write + commit: "<<timer.elapsed()<<" ms\n"<<std::flush;
    return 0;
  }
  CHECK(aero::sessionCsvHeader()=="Time,DeltaT,BlockStatus,MessageType,Bus,Rx_Cmd,Tx_Cmd,Tx_Status,Data,Rx_Status,BSW_err_str\n");
  auto cells=desktop::csvCells(QByteArray::fromStdString(aero::sessionCsvRecord(r,33355489000ULL)));
  CHECK(cells==QStringList({"00:00:00:48:745527","15390038","8100","BC --> RT","A","0947 -->RT 01-R-SA 10-WC 07","","","0Xaaaa  0X5042  0X1  0X3  0X14aa  0X5  0X7","800",""}));
  CHECK(desktop::csvTime(cells[0],true)==r.timestampNs);
  CHECK(desktop::csvTime("01:00:05:29:942996",true)==86400000000000ULL+329942996000ULL);
  CHECK(desktop::csvTime("00:00:48 745.527",false)==r.timestampNs);
  CHECK(aero::compactCsvCommand(0x2440,aero::NumericRadix::Hexadecimal)=="2440 -->RT 04-T-SA 02-WC 00");
  r.timestampNs=1000;CHECK(desktop::csvCells(QByteArray::fromStdString(aero::sessionCsvRecord(r,4000)))[1]=="-3");
  r.commandWord=0x2440;r.messageType=1;r.validity=1;r.words.assign(32,0xffff);
  cells=desktop::csvCells(QByteArray::fromStdString(aero::sessionCsvRecord(r)));
  CHECK(cells[5].isEmpty()&&cells[7].isEmpty()&&cells[9].isEmpty());CHECK(cells[6]=="2440 -->RT 04-T-SA 02-WC 00");CHECK(cells[8].split("  ").size()==32);
  for(auto base:{aero::NumericRadix::Hexadecimal,aero::NumericRadix::Decimal,aero::NumericRadix::Binary,aero::NumericRadix::Octal})for(unsigned n=0;n<65536;++n)CHECK(desktop::csvWord(QString::fromStdString(aero::rawNumber(n,base,true)),int(base))==n);
  const auto path=temporary.filePath("sample.csv");QFile file(path);CHECK(file.open(QIODevice::WriteOnly));file.write(QByteArray::fromStdString(aero::sessionCsvHeader()));file.write(QByteArray::fromStdString(aero::sessionCsvRecord(r)));file.close();CHECK(file.open(QIODevice::ReadOnly));desktop::CsvInput input(file);auto source=desktop::csvCells(desktop::csvRow(file));
  for(auto base:{aero::NumericRadix::Decimal,aero::NumericRadix::Binary,aero::NumericRadix::Octal}){auto converted=input.convert(source,16,base);auto restored=input.convert(converted,int(base),aero::NumericRadix::Hexadecimal);CHECK(restored==source);CHECK(restored[0]==source[0]&&restored[1]==source[1]);}
  bool failed=false;try{desktop::csvWord("65536",10);}catch(...){failed=true;}CHECK(failed);
  failed=false;try{input.convert(QStringList{"malformed"},16,aero::NumericRadix::Decimal);}catch(...){failed=true;}CHECK(failed);
  CHECK(desktop::csvCells(desktop::csvLine({"a,b","a\"b","", "line\nbreak"}))==QStringList({"a,b","a\"b","","line\nbreak"}));
  {QFile longFile(temporary.filePath("long.csv"));CHECK(longFile.open(QIODevice::WriteOnly));const QStringList expected{QString(5000,'x')+"\ncontinued", "tail"};longFile.write(desktop::csvLine(expected));longFile.close();CHECK(longFile.open(QIODevice::ReadOnly));CHECK(desktop::csvCells(desktop::csvRow(longFile))==expected);longFile.close();CHECK(longFile.open(QIODevice::WriteOnly|QIODevice::Truncate));longFile.write(QByteArray(1024*1024+1,'x')+'\n');longFile.close();CHECK(longFile.open(QIODevice::ReadOnly));bool rejected=false;try{desktop::csvRow(longFile);}catch(...){rejected=true;}CHECK(rejected);}
  CHECK(desktop::displayValue(1.123456789)=="1.123457"&&desktop::displayValue(-0.)=="0");
  CHECK(desktop::displayValue(3.402823466e38)=="3.402823e+38"&&desktop::displayValue(1.2e-8)=="1.2e-08");
  for(const auto layout:{aero::CsvLayout::Compact11,aero::CsvLayout::Legacy42}){
    const auto fixture=temporary.filePath("analysis.csv");QFile csv(fixture);CHECK(csv.open(QIODevice::WriteOnly|QIODevice::Truncate));
    const aero::CsvExportOptions options{layout,aero::NumericRadix::Octal};
    csv.write(QByteArray::fromStdString(aero::sessionCsvHeader(options)));
    auto sample=r;sample.channel=1;sample.bus=0;sample.source=1;sample.validity=5;sample.messageType=1;sample.commandWord=0x2440;sample.statusWord=0x2000;
    sample.words.assign(32,0);sample.words[0]=0x3f80;
    for(const auto t:{2000000ull,3000000ull,1000000ull}){sample.timestampNs=t;csv.write(QByteArray::fromStdString(aero::sessionCsvRecord(sample,{},options)));}
    csv.close();
    aero::Signal mapping;mapping.id="test";mapping.rt=4;mapping.sa=2;mapping.transmit=true;mapping.width=32;mapping.numericType="float32";mapping.scale=1;
    desktop::RecordedCsvRequest query;query.path=fixture;query.radix=8;query.route=aero::command(0x2440);query.traces={{"Raw",0},{"Engineering",-1,mapping,{},.5}};
    std::atomic_bool stop{false};const auto result=desktop::readRecordedCsv(query,stop);
    CHECK(result.error.isEmpty()&&result.series.size()==2);
    CHECK(result.series[0].statistics.count==3&&result.series[0].statistics.boundaries==1&&result.series[0].points.size()==4);
    CHECK(result.series[0].points[0].row==1&&result.series[0].points[2].row==0&&result.series[0].points[3].row==3);
    CHECK(result.series[1].statistics.mean==1.&&result.series[1].statistics.excursions==3);
    {QFile parsed(fixture);CHECK(parsed.open(QIODevice::ReadOnly));desktop::CsvInput input(parsed);auto row=desktop::csvCells(desktop::csvRow(parsed));
      auto noStatus=row;noStatus[7].clear();CHECK(!aero::decode(input.record(noStatus,8,1,1),mapping));
      if(layout==aero::CsvLayout::Legacy42){auto padded=row;for(int i=0;i<7;++i)padded<<QString();input.validate(padded);CHECK(input.convert(padded,8,aero::NumericRadix::Hexadecimal).size()==42);CHECK(aero::decode(input.record(padded,8,1,1),mapping)==1.);padded.back()="1";bool refused=false;try{input.validate(padded);}catch(...){refused=true;}CHECK(refused);}
      auto error=row;error[2]=QString::fromStdString(aero::rawNumber(0x8200,aero::NumericRadix::Octal));CHECK(!aero::decode(input.record(error,8,1,1),mapping));
      auto transfer=sample;transfer.commandWord=(5<<11)|(2<<5);transfer.commandWord2=0x2440;transfer.statusWord=4<<11;transfer.statusWord2=5<<11;transfer.validity=15;transfer.messageType=2;
      const auto rtRow=desktop::csvCells(QByteArray::fromStdString(aero::sessionCsvRecord(transfer,{},options)));
      const auto native=input.record(rtRow,8,1,1);CHECK(aero::interpret(native).rtToRt&&aero::decode(native,mapping)==1.);
    }
    query.first=2000000;CHECK(desktop::readRecordedCsv(query,stop).series[1].statistics.count==2);
    stop=true;CHECK(desktop::readRecordedCsv(query,stop).cancelled);
  }
  {const auto path=temporary.filePath("narrow.csv");QFile file(path);CHECK(file.open(QIODevice::WriteOnly));file.write(QByteArray::fromStdString(aero::sessionCsvHeader()));
    auto sample=r;sample.commandWord=0x2440;sample.words.assign(32,0);
    for(unsigned i=0;i<10000;++i){sample.timestampNs=1000000ull*i;sample.words[0]=i;file.write(QByteArray::fromStdString(aero::sessionCsvRecord(sample)));}file.close();
    desktop::RecordedCsvRequest query;query.path=path;query.route=aero::command(0x2440);query.first=9990000000ull;query.last=9999000000ull;query.traces={{"Raw",0}};std::atomic_bool cancel{false};
    const auto result=desktop::readRecordedCsv(query,cancel);CHECK(result.error.isEmpty()&&result.series[0].statistics.count==10&&result.series[0].points.size()==10);
  }
  std::cout<<"PASS: CSV golden rows, radix conversion, recorded CSV plots, range statistics and display precision\n";
  return 0;
}
