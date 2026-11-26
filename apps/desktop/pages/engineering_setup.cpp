#include "main_window.hpp"
#include "record_csv.hpp"
#include "ui_engineering_page.h"

void Window::engineering() {
  auto form=showForm<Ui::EngineeringPage>();
  form.loadProfileButton->hide();
  engineeringDetails=form.profileBadge;qualityTable=form.qualityTable;
  form.analysisSource->addItems({"External · CH1 / A","External · CH1 / B","Internal · CH2 / A","Internal · CH2 / B"});
  form.analysisSource->setCurrentIndex((analysisChannel-1)*2+analysisBus);
  connect(form.analysisSource,&QComboBox::currentIndexChanged,content,[this](int value){analysisChannel=value/2+1;analysisBus=value%2;updateEngineeringDetails();});
  connect(form.loadProfileButton,&QPushButton::clicked,content,[this]{navigate(12);});
  if(auto liveBtn=content->findChild<QPushButton *>("liveEngineeringButton"))
    connect(liveBtn,&QPushButton::clicked,content,[this]{liveEngineering();});
  form.openPlotsButton->setText("Plot selected →");
  connect(form.openPlotsButton,&QPushButton::clicked,content,[this]{recordedCsv.reset();plotRawColumn=-1;navigate(7);});
  const auto mappings=backend.mappings();
  qualityTable->setColumnCount(8);
  qualityTable->setHorizontalHeaderLabels({"Signal","Source words","Encoding","Unit","Conversion","Latest hex","Live value","Status"});
  qualityTable->setRowCount(mappings.size());qualityTable->verticalHeader()->hide();qualityTable->verticalHeader()->setDefaultSectionSize(36);
  qualityTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  const int widths[]{90,95,110,60,125,95,110,120};
  for(int i=0;i<8;++i){qualityTable->setColumnWidth(i,widths[i]);qualityTable->setColumnHidden(i,false);}
  for(unsigned row=0;row<mappings.size();++row){const auto &s=mappings[row];const auto last=s.word+(s.lsb+s.width+15)/16-1;
    const auto wordsText=s.word==last?QString("Payload %1").arg(s.word):QString("Payload %1–%2").arg(s.word).arg(last);
    const auto source=s.sourceWord==aero::SignalWord::Payload?wordsText:QString::fromStdString(std::string(aero::signalWordName(s.sourceWord)));
    const QStringList cells{QString::fromStdString(s.sourceSignalId.empty()?s.id:s.sourceSignalId),source,QString::fromStdString(s.numericType)+(s.numericType=="integer"?(s.signedValue?" · signed":" · unsigned"):""),QString::fromStdString(s.unit),s.conversionFormula.empty()?QString("x × %1 + %2").arg(desktop::displayValue(s.scale)).arg(desktop::displayValue(s.offset)):
      QString::fromStdString(s.conversionFormula),"—","—","Awaiting data"};
    for(int col=0;col<cells.size();++col){auto item=new QTableWidgetItem(cells[col]);item->setToolTip(QString::fromStdString(s.id)+"\n"+cells[col]);qualityTable->setItem(row,col,item);}
    qualityTable->setRowHidden(row,!inWorkspace(s));
  }
  form.rawBase->addItem("Hexadecimal",16);form.rawBase->addItem("Unsigned decimal",10);form.rawBase->addItem("Binary",2);form.rawBase->addItem("Octal",8);
  form.advancedPanel->hide();
  connect(form.advancedButton,&QToolButton::toggled,content,[form](bool on){form.advancedPanel->setVisible(on);form.advancedButton->setArrowType(on?Qt::DownArrow:Qt::RightArrow);});
  auto selected=std::make_shared<std::optional<aero::Record>>();
  const auto preview=[this,form,mappings,selected]{
    if(preferredSignalIndex>=mappings.size()){form.previewResult->setPlainText("Select a published mapping first.");return;}
    const auto &signal=mappings[preferredSignalIndex];aero::Record record;
    if(*selected)record=**selected;
    else {
      const auto parts=form.rawWords->toPlainText().trimmed().split(QRegularExpression("[\\s,;]+"),Qt::SkipEmptyParts);
      const auto span=(signal.lsb+signal.width+15)/16;
      if(parts.size()!=int(span)){form.previewResult->setPlainText(QString("Enter exactly %1 word(s) for this field.").arg(span));return;}
      std::vector<std::uint16_t> words;
      for(auto text:parts){if(text.startsWith("0x",Qt::CaseInsensitive)||text.startsWith("0b",Qt::CaseInsensitive)||text.startsWith("0o",Qt::CaseInsensitive))text=text.mid(2);bool ok=false;const auto value=text.toULongLong(&ok,form.rawBase->currentData().toInt());if(!ok||value>65535){form.previewResult->setPlainText("Each word must be an unsigned 16-bit value in the selected base.");return;}words.push_back(value);}
      record=aero::previewSignalRecord(signal,words,analysisChannel,analysisBus);
    }
    const auto value=aero::decode(record,signal,backend.exampleContext());auto unscaled=signal;unscaled.scale=1;unscaled.offset=0;unscaled.conversionFormula.clear();unscaled.preparedFormula.reset();
    const auto code=aero::decode(record,unscaled,backend.exampleContext());
    QStringList rows{*selected?"Selected record · native decoder":"Synthetic field preview · other words unspecified",QString::fromStdString(signal.id)};
    const auto decoded=aero::decodeValue(record,signal,backend.exampleContext());
    for(unsigned i=0;i<decoded.rawCount;++i){const auto word=decoded.raw[i];const auto label=signal.sourceWord==aero::SignalWord::Payload?QString("Word %1").arg(signal.word+i):QString::fromStdString(std::string(aero::signalWordName(signal.sourceWord)));
      rows<<QString("%1: 0x%2 · %3 · %4 · %5").arg(label,hexword(word)).arg(word).arg(QString::fromStdString(aero::rawNumber(word,aero::NumericRadix::Binary)),QString::fromStdString(aero::rawNumber(word,aero::NumericRadix::Octal)));}
    rows<<QString("Encoding: %1 · %2").arg(QString::fromStdString(signal.numericType),signal.leastSignificantWordFirst?"LSW first":"MSW first");
    if(value){
      const auto expression=signal.conversionFormula.empty()?
        QString("x × %1 + %2").arg(desktop::displayValue(signal.scale),desktop::displayValue(signal.offset)):
        QString::fromStdString(signal.conversionFormula);
      rows<<QString("Decoded: %1\nFormula: %2\nEngineering: %3 %4\nDecoded; physical plausibility requires configured limits / known reference.")
        .arg(desktop::displayValue(code.value_or(0)),expression,desktop::displayValue(*value),QString::fromStdString(signal.unit));
    }
    else rows<<"No engineering value: route, encoding, payload quality, or required words do not match.";
    form.previewResult->setPlainText(rows.join('\n'));
    form.previewResult->setProperty("decodedValue",value?QVariant(*value):QVariant());
    setProperty("nativeConversionPreview",rows.join('\n'));
  };
  connect(form.rawWords,&QPlainTextEdit::textChanged,content,[selected]{selected->reset();});
  connect(form.rawBase,&QComboBox::currentIndexChanged,content,[selected]{selected->reset();});
  connect(form.previewButton,&QPushButton::clicked,content,preview);
  form.useRecord->setText("Use latest / selected record");
  connect(form.useRecord,&QPushButton::clicked,content,[this,form,mappings,selected,preview]{
    if(preferredSignalIndex>=mappings.size())return;
    const auto &s=mappings[preferredSignalIndex];
    aero::Record match=backend.selected;
    auto matchesSignal=[&](const aero::Record &r){
      return aero::decode(r,s,backend.exampleContext()).has_value();
    };
    if(match.words.empty() || !matchesSignal(match)) {
      const auto &records=analysisData();
      for(auto it=records.rbegin();it!=records.rend();++it)if(matchesSignal(*it)){match=*it;backend.selected=match;break;}
      if(match.words.empty())for(auto it=records.rbegin();it!=records.rend();++it){match=*it;break;}
    }
    if(match.words.empty()){form.previewResult->setPlainText("Capture or load a matching recording first, then click Use latest / selected record.");return;}
    QStringList raw;for(unsigned i=s.word;i<s.word+(s.lsb+s.width+15)/16 && i<match.words.size();++i)raw<<QString::fromStdString(aero::rawNumber(match.words[i],static_cast<aero::NumericRadix>(form.rawBase->currentData().toUInt()),true));
    form.rawWords->setPlainText(raw.join(' '));*selected=match;preview();
  });
  connect(form.editRule,&QPushButton::clicked,this,&Window::manualMappings);
  connect(qualityTable,&QTableWidget::currentCellChanged,content,[this,form,mappings,selected](int row,int,int,int){if(row<0||row>=int(mappings.size()))return;preferredSignalIndex=row;selected->reset();const auto &s=mappings[row];if(s.channel)analysisChannel=s.channel;if(s.bus>=0)analysisBus=s.bus;const QSignalBlocker block(form.analysisSource);form.analysisSource->setCurrentIndex((analysisChannel-1)*2+analysisBus);form.ruleDetails->setText(QString("%1 bits · offset %2 · %3 · scale %4 · offset %5\nRoute: RT %6 / SA %7 / %8 · Word %9").arg(s.width).arg(s.lsb).arg(s.leastSignificantWordFirst?"LSW first":"MSW first").arg(desktop::displayValue(s.scale)).arg(desktop::displayValue(s.offset)).arg(s.rt).arg(s.sa).arg(s.transmit?"Tx":"Rx").arg(s.word));form.previewResult->setPlainText("Enter raw words or click Use latest / selected record, then preview.");updateEngineeringDetails();});
  if(preferredSignalIndex>=mappings.size())preferredSignalIndex=0;
  if(!mappings.empty())qualityTable->selectRow(preferredSignalIndex);
  form.engineeringSplitter->restoreState(settings.value("engineeringSplitter").toByteArray());
  connect(form.engineeringSplitter,&QSplitter::splitterMoved,content,[this,form]{settings.setValue("engineeringSplitter",form.engineeringSplitter->saveState());});
}
