#include "main_window.hpp"
#include "qualification_source.hpp"
#include "services/manual_mapping.hpp"
#include "services/profile_io.hpp"

bool Window::manualMappingTest(const QString &directory,const QString &pdf,const QString &runtime) {
  QDir().mkpath(directory);
  bool passed=true;
  QStringList notes;
  const auto check=[&](bool ok,const QString &message){
    passed&=ok;notes<<(ok?"PASS · ":"FAIL · ")+message;
    QSaveFile report(directory+"/manual-mapping.txt");
    if(report.open(QIODevice::WriteOnly)){report.write(notes.join('\n').toUtf8());report.commit();}
  };
  const auto wait=[&](auto ready,int timeout=5000){
    QElapsedTimer clock;clock.start();
    while(!ready() && clock.elapsed()<timeout) {
      QEventLoop loop;QTimer::singleShot(25,&loop,&QEventLoop::quit);loop.exec();
    }
    return ready();
  };
  unlocked=true;
  const auto draftsBefore=desktop::readManualDrafts();
  desktop::saveManualDrafts({});
  manualRegistry={};
  backend.profile.clear();
  manualMappings();
  const auto dialog=content->findChild<QWidget *>("manualMappingHost");
  if(!dialog){desktop::saveManualDrafts(draftsBefore);check(false,"Manual mapping editor opened");return false;}
  QApplication::processEvents();
  const auto button=[dialog](const char *name){return dialog->findChild<QPushButton *>(name);};
  const auto select=[](QWidget *root,const char *key,int value){
    if(auto input=root->findChild<QComboBox *>(key))input->setCurrentIndex(input->findData(value));
  };
  auto cards=[dialog]{return dialog->findChildren<QFrame *>("manualFieldCard");};
  if(cards().isEmpty())button("addManualCommand")->click();
  QApplication::processEvents();
  check(cards().size()==1 && dialog->findChild<QComboBox *>("manualSubsystem")!=nullptr,
        "Inline command route and expandable field card open directly without modal wizards");
  dialog->findChild<QComboBox *>("manualSubsystem")->setEditText("Thermal Control");
  select(dialog,"manualRt",3);
  select(dialog,"manualSa",2);
  dialog->findChild<QComboBox *>("manualDirection")->setCurrentIndex(0);

  // Add two more fields so 3 expanded field cards are visible and editable together.
  button("addManualField")->click();
  button("addManualField")->click();
  button("expandAllManualFields")->click();
  QApplication::processEvents();
  const auto allCards=cards();
  const bool allExpanded=allCards.size()==3 &&
    std::all_of(allCards.begin(),allCards.end(),[](QFrame *card){
      auto name=card->findChild<QLineEdit *>("fieldNameEdit");
      return name && name->isVisible();
    });
  check(allExpanded,"Expand all displays multiple editable fields simultaneously");
  if(allCards.size()!=3)return false;

  const auto configureCard=[&](QFrame *card,QString name,int word,int typeIdx,int orderIdx,
                               QString scale,QString offset,QString unit,QString raw,QString value) {
    card->findChild<QLineEdit *>("fieldNameEdit")->setText(name);
    select(card,"fieldWordCombo",word);
    card->findChild<QComboBox *>("fieldTypeCombo")->setCurrentIndex(typeIdx);
    if(orderIdx>=0)card->findChild<QComboBox *>("fieldOrderCombo")->setCurrentIndex(orderIdx);
    card->findChild<QLineEdit *>("fieldUnitEdit")->setText(unit);
    card->findChild<QLineEdit *>("fieldScaleEdit")->setText(scale);
    card->findChild<QLineEdit *>("fieldOffsetEdit")->setText(offset);
    card->findChild<QLineEdit *>("fieldRawEdit")->setText(raw);
    card->findChild<QLineEdit *>("fieldExpectedEdit")->setText(value);
  };
  const QString degree="degC";
  // Words 2, 3, 4 (0-based) match aero::qualificationRecord(n, true).
  configureCard(allCards[0],"Temperature",2,0,0,"0.5","-10",degree,"0064","41");
  configureCard(allCards[1],"Signed correction",3,1,0,{},{},"count","FFFE","-2");
  configureCard(allCards[2],"Pressure",4,2,0,{},{},"kPa","41480000","12.5");
  button("publishManual")->click();
  check(backend.profile.empty(),"Wrong expected value and wrong word order block publication");
  configureCard(allCards[0],"Temperature",2,0,0,"0.5","-10",degree,"0064","40");
  configureCard(allCards[2],"Pressure",4,2,1,{},{},"kPa","41480000","12.5");
  button("validateManual")->click();
  QApplication::processEvents();
  button("publishManual")->click();
  check(backend.profile.size()==3 && manualRegistry["commands"].toArray().size()==1,"One publication persists all fields, 0-based words, and contiguous hex examples");
  if(backend.profile.size()!=3)return false;
  dialog->grab().save(directory+"/mapping.png");
  {
    auto noHex=manualRegistry["commands"].toArray().at(0).toObject();
    auto fields=noHex["fields"].toArray();
    for(int i=0;i<fields.size();++i){auto f=fields[i].toObject();f.remove("raw");f.remove("expected");f.remove("examples");fields.replace(i,f);}
    noHex["fields"]=fields;
    check(desktop::checkManualMapping(noHex).error.isEmpty(),"Publishing works without mandatory hex test samples");

    auto definition=manualRegistry["commands"].toArray().at(0).toObject();
    fields=definition["fields"].toArray();
    auto overlap=fields.at(1).toObject();overlap["word"]=2;fields.replace(1,overlap);definition["fields"]=fields;
    check(desktop::checkManualMapping(definition).error.contains("overlaps"),"Overlapping mapped bits require correction or alias opt-in");
    overlap["allow_overlap"]=true;fields.replace(1,overlap);definition["fields"]=fields;
    check(desktop::checkManualMapping(definition).error.isEmpty(),"Explicit alias opt-in allows overlapping discrete/full-word fields");

    auto taggedA=backend.profile.front(),taggedB=backend.profile.front();
    taggedA.matchWord=0;taggedA.matchValue=0x8888;
    taggedB.matchWord=0;taggedB.matchValue=0x9999;
    check(!desktop::overlappingManualRoute(taggedA,taggedB),"Distinct payload frame tags allow multiplexed subaddresses");
    auto instance=backend.profile.front();instance.channel=2;instance.bus=1;
    check(desktop::overlappingManualRoute(backend.profile.front(),instance),"Wildcard channel and bus cannot hide a conflicting definition");
  }
  {
    auto command=manualRegistry["commands"].toArray().at(0).toObject();
    auto field=command["fields"].toArray().at(0).toObject();
    field["formula"]="pow(x, 2) / 100";
    field["examples"]=QJsonArray{QJsonObject{{"raw","000A"},{"expected","1"}},
                                QJsonObject{{"raw","0014"},{"expected","4"}}};
    command["fields"]=QJsonArray{field};
    auto valid=desktop::checkManualMapping(command);
    check(valid.error.isEmpty()&&valid.rules.size()==1,"Two distinct examples validate a nonlinear formula");
    if(!valid.rules.empty()){
      const auto profile=desktop::profileToJson(valid.rules);
      std::vector<aero::Signal> restored;
      check(profile["schema"]=="aerotrace.icd.profile.v3"&&desktop::profileFromJson(profile,restored).isEmpty()
        &&restored.front().conversionFormula=="pow(x, 2) / 100","Formula profiles preserve their version and expression");
    }
  }
  dialog->close();
  {
    Window reopened;
    check(reopened.backend.profile.size()==3 && reopened.manualRegistry["commands"].toArray().at(0).toObject()["fields"].toArray().size()==3,"A new window restores the selected profile and examples from disk");
  }
  const auto source=aero::qualificationRecord(0,true);
  const std::array<double,3> expected{40,-2,12.5};
  auto variants=source;variants.words.resize(8);variants.commandWord|=8;
  bool distinct=true;
  for(unsigned index=0;index<3;++index) {
    const auto &signal=backend.profile[index];
    distinct&=aero::decode(source,signal)==expected[index]&&!aero::decode(variants,signal);
    auto wrong=source;wrong.commandWord^=1<<10;distinct&=!aero::decode(wrong,signal);
    auto invalid=source;invalid.statusWord|=0x400;distinct&=!aero::decode(invalid,signal);
  }
  check(distinct,"Native decode rejects wrong count, direction and invalid status");

  auto run=[&](const QString &name,bool hardware) {
    auto config=backend.captureConfig;
    config.hardware=hardware;config.enabled={true,true,true,true};config.logicalDevices={0,1};
    config.runtime=std::filesystem::path(runtime.toStdWString());
    config.receiveMode=aero::DdcReceiveMode::MTI;
    auto file=std::filesystem::path((directory+"/"+name+"-"+QUuid::createUuid().toString(QUuid::Id128)+".atd").toStdWString());
    backend.acquisition.setProfile(backend.profile);
    backend.selected=source;
    liveEngineering();
    auto view=findChild<QDialog *>("liveEngineeringDialog");
    auto charts=view?view->findChild<QListView *>("liveFieldCharts"):nullptr;
    const auto started=backend.acquisition.startConfigured(file,config,hardware?nullptr:
      std::make_unique<aero::QualificationSource>(200,std::make_shared<aero::QualificationCounter>(),true));
    const auto drawn=started && charts && wait([&]{return charts->property("visibleTracesWithData").toInt()==3;});
    std::array<double,3> actual{};bool values=drawn;
    if(drawn) {
      const auto frame=backend.acquisition.liveFrame();
      for(unsigned i=0;i<3;++i) {
        const auto &value=frame->sourceValues[0][i];
        values&=value.value.has_value();actual[i]=value.value.value_or(0);
      }
      view->grab().save(directory+"/"+name+"-charts.png");
    }
    check(drawn && values && actual==expected,name+" displays all three live conversions and charts");
    if(hardware && !started)check(false,QString::fromStdString(backend.acquisition.metrics(false).error));
    backend.stopRecording();
    if(view)view->close();
    check(!backend.acquisition.metrics(false).failed,name+" finalized cleanly");
  };
  run("simulator",false);
  if(!runtime.isEmpty()) {
    qputenv("AEROTRACE_TEST_DDC_MANUAL_MAPPING","1");
    qputenv("AEROTRACE_TEST_DDC_BOTH_BUSES","1");
    run("ddc-sdk-fixture",true);
    qunsetenv("AEROTRACE_TEST_DDC_MANUAL_MAPPING");
    qunsetenv("AEROTRACE_TEST_DDC_BOTH_BUSES");
  }

  Q_UNUSED(pdf);
  QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
  if(backend.profile.size()==3) {
    const auto field=QString::fromStdString(backend.profile.front().id);
    const auto original=desktop::profileToJson(backend.profile);
    const auto tryDelete=[this,field](bool firstYes,const QString &typed) {
      QTimer confirmations;
      confirmations.setInterval(20);
      bool approvedFirst=false;
      connect(&confirmations,&QTimer::timeout,this,[&] {
        if(auto message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());message&&!approvedFirst) {
          approvedFirst=true;
          message->button(firstYes?QMessageBox::Yes:QMessageBox::Cancel)->click();
        } else if(auto input=qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
          confirmations.stop();input->setTextValue(typed);input->accept();
        }
      });
      confirmations.start();
      deleteIcdMapping("field",field,"Temperature");
    };
    tryDelete(false,{});
    check(desktop::profileToJson(backend.profile)==original,"Cancelling the deletion preview leaves mappings intact");
    tryDelete(true,"wrong");
    check(desktop::profileToJson(backend.profile)==original,"Incorrect typed confirmation leaves mappings intact");
    tryDelete(true,"DELETE");
    check(backend.profile.size()==2&&manualRegistry["commands"].toArray().at(0).toObject()["fields"].toArray().size()==2,
      "Two confirmations remove the selected field and preserve sibling mappings");
  }
  desktop::saveManualDrafts(draftsBefore);
  return passed;
}
