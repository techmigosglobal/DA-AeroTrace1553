#include "services/card_manager.hpp"
#include <QtCore>
#include <iostream>
#define CHECK(x) do { if(!(x))throw std::runtime_error(#x); } while(false)
int main(int argc,char **argv) {
  QCoreApplication app(argc,argv);QTemporaryDir temp;
  try {
    CHECK(temp.isValid());const auto root=temp.path()+"/DDC custom path";CHECK(QDir().mkpath(root));
    const auto exe=root+"/ddccm.exe";QFile file(exe);CHECK(file.open(QIODevice::WriteOnly));file.write("test-only placeholder; never execute");file.close();
    const auto found=desktop::resolveCardManager({temp.path()+"/stale/ddccm.exe",exe});
    CHECK(found.path==QFileInfo(exe).canonicalFilePath()&&found.error.isEmpty()&&found.examined.size()==2);
    CHECK(desktop::resolveCardManager({exe},temp.path()+"/missing.exe").path.isEmpty());
    CHECK(desktop::resolveCardManager({},'"'+exe+'"').path==found.path);
    CHECK(desktop::resolveCardManager({exe,exe}).examined.size()==1);
    CHECK(desktop::resolveCardManager({temp.path()}).path.isEmpty());
    CHECK(desktop::resolveCardManager({}).error.contains("not found"));
    const auto missing=desktop::launchCardManager(temp.path()+"/absent.exe");
    CHECK(!missing.launched&&!missing.cancelled&&missing.errorCode==2);
    std::cout<<"Card Manager path selection and missing-launch checks passed; no vendor process was launched.\n";
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
