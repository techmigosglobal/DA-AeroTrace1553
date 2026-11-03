#include "services/session_store.hpp"
#include "services/ddc_discovery.hpp"
#include "workbench.hpp"
#include "record_csv.hpp"
#include <QtCore>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(int argc,char **argv) {
  QCoreApplication app(argc,argv);QCoreApplication::setOrganizationName("AeroTraceTests");QStandardPaths::setTestModeEnabled(true);
  QTemporaryDir temp;
  try {
    CHECK(desktop::classifyDdcAssignment(0,{})["state"]=="empty");
    CHECK(desktop::ddcAssignmentMatchesDevice(R"(\\?\usb#vid_4ddc&pid_0300#serial#{driver-guid})",R"(USB\VID_4DDC&PID_0300\SERIAL)"));
    CHECK(!desktop::ddcAssignmentMatchesDevice(R"(\\?\usb#vid_4ddc&pid_0300#oldserial#{driver-guid})",R"(USB\VID_4DDC&PID_0300\NEWSERIAL)"));
    CHECK(!desktop::ddcAssignmentMatchesDevice("",""));

    CHECK(desktop::classifyDdcAssignment(0,{{"DeviceID",0}})["state"]=="malformed");
    CHECK(desktop::classifyDdcAssignment(0,{{"DeviceID",0},{"Channel",0},{"Family",0}})["state"]=="candidate");
    CHECK(desktop::classifyDdcAssignment(0,{{"DeviceID","vendor-format"},{"Channel",1},{"Family",3}})["state"]=="candidate");
    CHECK(desktop::discoverySummary({{"physical_presence","unknown"},{"discovery_error","test failure"}}).contains("presence unknown"));
    CHECK(desktop::ddcDriverBinding("USB\\VID_4DDC&PID_5590","","",0,"started").contains("not bound"));
    CHECK(desktop::ddcDriverBinding("USB\\VID_4DDC&PID_5590","other","oem61.inf",0,"started").contains("Unexpected service"));
    CHECK(desktop::ddcDriverBinding("USB\\VID_4DDC&PID_5590","e2mausb","oem61.inf",0,"started").contains("SDK access unverified"));
    CHECK(desktop::ddcDriverBinding("USB\\VID_4DDC&PID_5590","e2mausb","oem61.inf",10,"not_started").contains("PnP problem"));
    aero::CaptureConfig simulated;simulated.hardware=false;for(unsigned n=0;n<64;++n){const auto r=aero::simulationRecord(simulated,n);CHECK((r.statusWord>>11)==aero::command(r.commandWord).rt);}
    aero::RtActivity activity;aero::Record observed;observed.channel=1;observed.source=1;observed.commandWord=(3<<11)|(2<<5)|1;observed.statusWord=3<<11;observed.validity=1;activity.observe(observed);CHECK(activity.channels[0][3].responses==0);
    observed.validity|=4;activity.observe(observed);CHECK(activity.channels[0][3].responses==1);observed.flags=1;activity.observe(observed);CHECK(activity.channels[0][3].responses==1);observed.flags=0;
    observed.commandWord=2<<5;observed.statusWord=0;activity.observe(observed);CHECK(activity.channels[0][0].responses==1);
    observed.commandWord=(31<<11)|(2<<5)|1;observed.statusWord=31<<11;activity.observe(observed);CHECK(activity.broadcasts[0]==1);
    observed.channel=2;observed.commandWord=(5<<11)|(2<<5)|1;observed.commandWord2=(3<<11)|(1<<10)|(2<<5)|1;observed.statusWord=3<<11;observed.statusWord2=5<<11;observed.validity=15;activity.observe(observed);CHECK(activity.channels[1][3].responses==1&&activity.channels[1][5].responses==1);
    CHECK(!desktop::validateRunName("CON").isEmpty()&&!desktop::validateRunName("a/b").isEmpty()&&desktop::validateRunName("bench_123").isEmpty());
    CHECK(temp.isValid());const auto path=temp.path()+"/large.atd",output=temp.path()+"/exports";
    aero::Recorder writer;CHECK(writer.open(std::filesystem::path(path.toStdWString())));
    constexpr unsigned total=220008;
    for(unsigned i=0;i<total;++i)CHECK(writer.append(aero::synthetic(i)));CHECK(writer.close());
    QFile status(path+".status.txt");CHECK(status.open(QIODevice::WriteOnly));status.write("integrity=CLOSED\r\nerror=\r\n");status.close();
    aero::CaptureConfig config;config.hardware=false;CHECK(desktop::describeSession(path,config).isEmpty());
    auto result=desktop::exportSession(path,output);CHECK(result.error.isEmpty() && !result.incomplete && result.run==1 && result.internal==total/2 && result.external==total/2);
    for(const auto &name:{"run1_INT.csv","run1_EXT.csv"}){QFile csv(output+"/"+name);CHECK(csv.open(QIODevice::ReadOnly));quint64 lines=0;while(!csv.atEnd()){csv.readLine();++lines;}CHECK(lines==total/2+1);}
    CHECK(QDir(output).entryList(QDir::Files|QDir::Hidden).size()==2);CHECK(QFileInfo(result.manifest).absolutePath()==temp.path());
    QFile published(output+"/run1_EXT.csv");CHECK(published.open(QIODevice::ReadWrite));
    const auto oldModified=QDateTime::currentDateTimeUtc().addSecs(-3600);CHECK(published.setFileTime(oldModified,QFileDevice::FileModificationTime));published.close();
    const auto preservedModified=QFileInfo(published).lastModified();
    auto again=desktop::exportSession(path,output);CHECK(again.error.isEmpty()&&again.run==1&&QFileInfo(published).lastModified()==preservedModified);
    aero::Workbench backend;auto opened=backend.loadRecording(std::filesystem::path(path.toStdWString()));CHECK(opened.error.empty());CHECK(backend.replayCount()==total&&backend.replayRecords.size()<=8192);
    CHECK(backend.seek(total-1)&&backend.selected.id==total&&backend.replayRecords.size()<=8192);
    CHECK(backend.seek(0)&&backend.selected.id==1);backend.replayPlaying=true;backend.advance(830);CHECK(backend.replayIndex==10);
    CHECK(!backend.exportData(aero::ExportFormat::RawCsv,0,UINT64_MAX).error.empty());backend.recordingIndex.reset();
    // Exact values taken from the supplied EXT CSV (seven-word BC-to-RT message).
    aero::Record edge;edge.source=1;edge.validity=5;edge.messageType=0;edge.channel=1;edge.timestampNs=48745527000ULL;
    edge.commandWord=0x0947;edge.statusWord=0x0800;edge.vendorStatus=0x8100;edge.words={0xaaaa,0x5042,1,3,0x14aa,5,7};
    const auto columns=QString::fromStdString(aero::sessionCsvHeader()).trimmed().split(',');
    const auto values=QString::fromStdString(aero::sessionCsvRecord(edge,33355489000ULL)).trimmed().split(',');
    CHECK(columns.size()==42&&values.size()==42&&columns[0]=="DateTime"&&columns[41]=="DataWord_31");
    CHECK(values.mid(0,10)==QStringList({"00:00:48 745.527","15390038","8100","BC->RT","A","0947 --> 01-R-10-07","","","0800","7"}));
    CHECK(values[10]=="aaaa"&&values[11]=="5042"&&values[16]=="0007"&&values[17]=="-"&&values[41]=="-");
    // Thirty-two-word RT-to-BC sample, Bus B, with a missing status that must remain blank.
    edge.commandWord=0x0fc0;edge.messageType=1;edge.words.assign(32,0xffff);edge.bus=1;edge.validity=1;
    const auto tx=QString::fromStdString(aero::sessionCsvRecord(edge)).trimmed().split(',');
    CHECK(tx.size()==42&&tx[1]=="0"&&tx[3]=="RT->BC"&&tx[4]=="B"&&tx[5].isEmpty());
    CHECK(tx[6]=="0fc0 --> 01-T-30-00"&&tx[7].isEmpty()&&tx[8].isEmpty()&&tx[9]=="32"&&tx[41]=="ffff");
    edge.validity=5;CHECK(QString::fromStdString(aero::sessionCsvRecord(edge)).split(',')[7]=="0800");
    // RT-to-RT command/status order follows SDK presence flags, including absent second status.
    edge.commandWord=(5<<11)|(2<<5)|1;edge.commandWord2=(3<<11)|(1<<10)|(7<<5)|1;edge.messageType=2;
    edge.statusWord=3<<11;edge.statusWord2=5<<11;edge.validity=15;edge.words={1};
    const auto rt=QString::fromStdString(aero::sessionCsvRecord(edge)).split(',');
    CHECK(rt[3]=="RT->RT"&&rt[5]=="2841 --> 05-R-02-01"&&rt[6]=="1ce1 --> 03-T-07-01"&&rt[7]=="1800"&&rt[8]=="2800");
    edge.timestampNs=1000;CHECK(QString::fromStdString(aero::sessionCsvRecord(edge,4000)).split(',')[1]=="-3");
    // Deltas follow each exported channel across Bus A/B interleaving.
    const auto timed=temp.path()+"/timed.atd";CHECK(writer.open(std::filesystem::path(timed.toStdWString())));
    edge=aero::synthetic(0);edge.timestampNs=100000;edge.channel=1;CHECK(writer.append(edge));
    edge.channel=2;edge.timestampNs=70000;CHECK(writer.append(edge));
    edge.channel=1;edge.bus=1;edge.timestampNs=120000;CHECK(writer.append(edge));
    edge.channel=2;edge.timestampNs=65000;CHECK(writer.append(edge));CHECK(writer.close());
    QFile timedStatus(timed+".status.txt");CHECK(timedStatus.open(QIODevice::WriteOnly));timedStatus.write("integrity=CLOSED\nerror=\n");timedStatus.close();
    const auto timedOutput=temp.path()+"/timed-export";CHECK(desktop::exportSession(timed,timedOutput).error.isEmpty());
    for(const auto &pair:QList<QPair<QString,QString>>{{"EXT","20"},{"INT","-5"}}){QFile csv(timedOutput+"/run1_"+pair.first+".csv");CHECK(csv.open(QIODevice::ReadOnly));csv.readLine();CHECK(csv.readLine().split(',')[1]=="0");CHECK(csv.readLine().split(',')[1]==pair.second.toUtf8());}
    QFile clockManifest(timed+".export.json");CHECK(clockManifest.open(QIODevice::ReadOnly));CHECK(QJsonDocument::fromJson(clockManifest.readAll()).object()["internal_clock_backwards"]=="1");
    // Unrelated changed output is never silently overwritten.
    QFile changed(output+"/run1_INT.csv");CHECK(changed.open(QIODevice::Append));changed.write("changed\n");changed.close();CHECK(!desktop::exportSession(path,output).error.isEmpty());
    // A second session reserves the next pair; empty Internal still has a CSV header.
    auto second=temp.path()+"/second.atd";CHECK(writer.open(std::filesystem::path(second.toStdWString())));auto record=aero::synthetic(0);CHECK(writer.append(record)&&writer.close());
    CHECK(desktop::describeSession(second,config).isEmpty());auto two=desktop::exportSession(second,output);CHECK(two.error.isEmpty()&&two.run==2&&two.internal==0&&two.external==1);
    // A committed pair is idempotent and never replaced, even if another app holds it open.
#ifdef _WIN32
    auto blocked=output+"/run2_INT.csv";
    auto handle=CreateFileW(reinterpret_cast<LPCWSTR>(blocked.utf16()),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);CHECK(handle!=INVALID_HANDLE_VALUE);
    CHECK(desktop::exportSession(second,output).error.isEmpty());CloseHandle(handle);
#endif
    // A failed destination choice can be retried after removing its directory collision.
    const auto retry=temp.path()+"/retry.atd",retryOutput=temp.path()+"/retry-export";CHECK(writer.open(std::filesystem::path(retry.toStdWString()))&&writer.close());
    CHECK(desktop::describeSession(retry,config).isEmpty());QDir().mkpath(retryOutput+"/run1_INT.csv");
    CHECK(!desktop::exportSession(retry,retryOutput,{},"run1").error.isEmpty());CHECK(QDir(retryOutput+"/run1_INT.csv").removeRecursively());
    CHECK(desktop::exportSession(retry,retryOutput,{},"run1").error.isEmpty());

    // Missing/unwritable destination must retain the source recording.
    QFile file(temp.path()+"/not-a-folder");CHECK(file.open(QIODevice::WriteOnly));file.close();CHECK(!desktop::exportSession(second,file.fileName()+"/nested").error.isEmpty()&&QFileInfo::exists(second));
    CHECK(!desktop::exportSession(temp.path()+"/absent.atd",output).error.isEmpty());
    QFile corrupt(second);CHECK(corrupt.open(QIODevice::Append));corrupt.write("bad");corrupt.close();auto recovered=desktop::exportSession(second,output);CHECK(!recovered.error.isEmpty()&&recovered.recovered&&recovered.incomplete);
    auto recoveredCopy=temp.path()+"/recovered.atd";CHECK(QFile::copy(second,recoveredCopy));auto recoveredNew=desktop::exportSession(recoveredCopy,temp.path()+"/recovered-export");CHECK(recoveredNew.error.isEmpty()&&recoveredNew.recovered&&recoveredNew.incomplete);
    auto renamed=temp.path()+"/renamed.atd";CHECK(desktop::renameRecording(second,renamed).isEmpty());CHECK(QFileInfo::exists(renamed+".session.json")&&!QFileInfo::exists(second+".session.json"));
    auto custom=temp.path()+"/custom.atd";CHECK(writer.open(std::filesystem::path(custom.toStdWString()))&&writer.close());CHECK(desktop::describeSession(custom,config).isEmpty());
    auto named=desktop::exportSession(custom,output,{},"SES_Bench_01");CHECK(named.error.isEmpty()&&named.runName=="SES_Bench_01"&&QFileInfo::exists(output+"/SES_Bench_01_INT.csv"));CHECK(desktop::exportSession(custom,output,{},"SES_Bench_01").error.isEmpty());
    // Existing outputs also reserve names case-insensitively on a case-sensitive test host.
    auto colliding=temp.path()+"/collision.atd";CHECK(QFile::copy(custom,colliding));CHECK(!desktop::exportSession(colliding,output,{},"ses_bench_01").error.isEmpty());
    CHECK(desktop::suggestedRunName(output)=="run3");
    const auto scan=desktop::discoverDdc({});CHECK(scan.contains("devices")&&scan.contains("runtime_available"));
    std::cout<<"PASS: 220008-record full export, indexed replay, counts, naming, empty channel, collision, failed commit/retry, recovery, rename, Windows discovery\n";
  }catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}
}
