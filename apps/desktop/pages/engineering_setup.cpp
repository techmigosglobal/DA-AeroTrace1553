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
  connect(form.loadProfileButton,&QPushButton::clicked,content,[this]{manageIcds();});
  connect(form.openPlotsButton,&QPushButton::clicked,content,[this]{recordedCsv.reset();plotRawColumn=-1;navigate(7);});
  const auto mappings=backend.mappings();
  qualityTable->setColumnCount(5);qualityTable->setHorizontalHeaderLabels({"Signal","Words (0-based)","Encoding","Unit","Scale / offset"});
  qualityTable->setRowCount(mappings.size());qualityTable->verticalHeader()->hide();qualityTable->verticalHeader()->setDefaultSectionSize(36);
  qualityTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  const int widths[]{90,120,110,70,140};for(int i=0;i<5;++i)qualityTable->setColumnWidth(i,widths[i]);qualityTable->setColumnHidden(4,true);
  for(unsigned row=0;row<mappings.size();++row){const auto &s=mappings[row];const auto last=s.word+(s.lsb+s.width+15)/16-1;
    const QStringList cells{QString::fromStdString(s.sourceSignalId.empty()?s.id:s.sourceSignalId),QString("%1–%2").arg(s.word).arg(last),QString::fromStdString(s.numericType)+(s.numericType=="integer"?(s.signedValue?" · signed":" · unsigned"):""),QString::fromStdString(s.unit),QString("×%1 + %2").arg(desktop::displayValue(s.scale)).arg(desktop::displayValue(s.offset))};
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
      record.channel=analysisChannel;record.bus=analysisBus;record.commandWord=(signal.rt<<11)|(signal.transmit?0x400:0)|(signal.sa<<5)|((signal.expectedWordCount?signal.expectedWordCount:signal.word+span)&31);
      record.statusWord=signal.rt<<11;record.words.resize(signal.expectedWordCount?signal.expectedWordCount:signal.word+span);
      for(int i=0;i<parts.size();++i){auto text=parts[i];if(text.startsWith("0x",Qt::CaseInsensitive)||text.startsWith("0b",Qt::CaseInsensitive)||text.startsWith("0o",Qt::CaseInsensitive))text=text.mid(2);bool ok=false;const auto value=text.toULongLong(&ok,form.rawBase->currentData().toInt());if(!ok||value>65535){form.previewResult->setPlainText("Each word must be an unsigned 16-bit value in the selected base.");return;}record.words[signal.word+i]=value;}
    }
    const auto value=aero::decode(record,signal,backend.exampleContext());auto unscaled=signal;unscaled.scale=1;unscaled.offset=0;
    const auto code=aero::decode(record,unscaled,backend.exampleContext());
    QStringList rows{*selected?"Selected record · native decoder":"Synthetic field preview · other words unspecified",QString::fromStdString(signal.id)};
    const auto span=(signal.lsb+signal.width+15)/16;
    for(unsigned i=signal.word;i<signal.word+span && i<record.words.size();++i){const auto word=record.words[i];rows<<QString("Word %1: 0x%2 · %3 · %4 · %5").arg(i).arg(hexword(word)).arg(word).arg(QString::fromStdString(aero::rawNumber(word,aero::NumericRadix::Binary)),QString::fromStdString(aero::rawNumber(word,aero::NumericRadix::Octal)));}
    rows<<QString("Encoding: %1 · %2").arg(QString::fromStdString(signal.numericType),signal.leastSignificantWordFirst?"LSW first":"MSW first");
    if(value){rows<<QString("Decoded: %1\nEngineering: %2 × %3 + %4 = %5 %6\nDecoded; physical plausibility requires configured limits / known reference.").arg(desktop::displayValue(code.value_or(0))).arg(desktop::displayValue(code.value_or(0))).arg(desktop::displayValue(signal.scale)).arg(desktop::displayValue(signal.offset)).arg(desktop::displayValue(*value)).arg(QString::fromStdString(signal.unit));}
    else rows<<"No engineering value: route, encoding, payload quality, or required words do not match.";
    form.previewResult->setPlainText(rows.join('\n'));
    form.previewResult->setProperty("decodedValue",value?QVariant(*value):QVariant());
    setProperty("nativeConversionPreview",rows.join('\n'));
  };
  connect(form.rawWords,&QPlainTextEdit::textChanged,content,[selected]{selected->reset();});
  connect(form.rawBase,&QComboBox::currentIndexChanged,content,[selected]{selected->reset();});
  connect(form.previewButton,&QPushButton::clicked,content,preview);
  connect(form.useRecord,&QPushButton::clicked,content,[this,form,mappings,selected,preview]{if(preferredSignalIndex>=mappings.size())return;const auto &s=mappings[preferredSignalIndex];const auto &r=backend.selected;if(r.words.empty()){form.previewResult->setPlainText("Select a saved/replay record in Sessions or the monitor first.");return;}QStringList raw;for(unsigned i=s.word;i<s.word+(s.lsb+s.width+15)/16 && i<r.words.size();++i)raw<<QString::fromStdString(aero::rawNumber(r.words[i],static_cast<aero::NumericRadix>(form.rawBase->currentData().toUInt()),true));form.rawWords->setPlainText(raw.join(' '));*selected=r;preview();});
  connect(form.editRule,&QPushButton::clicked,content,[this]{reviewSignals();});
  connect(form.openEvidence,&QPushButton::clicked,content,[this,mappings]{if(preferredSignalIndex>=mappings.size())return;const auto &s=mappings[preferredSignalIndex];if(!QRegularExpression("^[0-9a-f]{64}$").match(QString::fromStdString(s.sourceHash)).hasMatch()){toast("No valid source PDF identity is attached to this mapping.");return;}auto url=QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/Documents/"+QString::fromStdString(s.sourceHash)+".pdf");url.setFragment("page="+QString::number(s.sourcePage));if(QFileInfo::exists(url.toLocalFile()))QDesktopServices::openUrl(url);else toast("Source evidence is not available locally.");});
  connect(qualityTable,&QTableWidget::currentCellChanged,content,[this,form,mappings,selected](int row,int,int,int){if(row<0||row>=int(mappings.size()))return;preferredSignalIndex=row;selected->reset();const auto &s=mappings[row];if(s.channel)analysisChannel=s.channel;if(s.bus>=0)analysisBus=s.bus;const QSignalBlocker block(form.analysisSource);form.analysisSource->setCurrentIndex((analysisChannel-1)*2+analysisBus);form.ruleDetails->setText(QString("%1 bits · offset %2\n%3 · scale %4 · offset %5\n%6 · page %7").arg(s.width).arg(s.lsb).arg(s.leastSignificantWordFirst?"LSW first":"MSW first").arg(desktop::displayValue(s.scale)).arg(desktop::displayValue(s.offset)).arg(QString::fromStdString(s.sourceDocument)).arg(s.sourcePage));form.previewResult->setPlainText("Enter raw words or use a selected record, then preview.");updateEngineeringDetails();});
  if(preferredSignalIndex>=mappings.size())preferredSignalIndex=0;
  if(!mappings.empty())qualityTable->selectRow(preferredSignalIndex);
  form.engineeringSplitter->restoreState(settings.value("engineeringSplitter").toByteArray());
  connect(form.engineeringSplitter,&QSplitter::splitterMoved,content,[this,form]{settings.setValue("engineeringSplitter",form.engineeringSplitter->saveState());});
}
