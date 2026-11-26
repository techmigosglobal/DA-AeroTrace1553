#include "main_window.hpp"
#include "services/csv_conversion.hpp"
#include "services/session_store.hpp"
#include "services/operator_style.hpp"
#include "ui_plots_page.h"

namespace {
struct CsvJobResult {QString error;QStringList header;QList<QStringList> rows,raw;quint64 processed{};unsigned verifiedChannel{};int radix{};bool compact{},generic{},more{},cancelled{};};
}
void Window::csvTools(const QString &recording) {
  auto dialog=new QDialog(this);dialog->setObjectName("savedCsvTools");dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("CSV Viewer");dialog->resize(1100,680);
  auto outer=new QVBoxLayout(dialog);outer->setContentsMargins(14,12,14,12);
  outer->addWidget(buildCsvViewerWidget(dialog,recording),1);
  dialog->show();
}

QWidget *Window::buildCsvViewerWidget(QWidget *parent,const QString &recording) {
  auto dialog=new QWidget(parent);dialog->setObjectName("csvViewerHost");
  dialog->setStyleSheet(
    "#csvViewerHost{background:#f3f7fb;}"
    "#csvViewerHost QFrame[card=\"true\"]{background:#ffffff;border:1px solid #d4e0e9;border-radius:10px;}"
    "#csvViewerHost QTableWidget{background:#ffffff;border:1px solid #d4e0e9;border-radius:8px;}"
    "#csvViewerHost QPushButton{border-radius:8px;padding:6px 14px;}"
  );
  auto layout=new QVBoxLayout(dialog);layout->setContentsMargins(0,0,0,0);layout->setSpacing(10);
  auto card=new QFrame(dialog);card->setProperty("card",true);
  auto cardLayout=new QVBoxLayout(card);cardLayout->setContentsMargins(16,14,16,14);cardLayout->setSpacing(12);
  auto topRow=new QHBoxLayout;topRow->setSpacing(10);
  auto browse=new QPushButton("Open CSV",card);browse->setProperty("primary",true);desktop::actionIcon(browse,"folder",true);
  topRow->addStretch(1);topRow->addWidget(browse);cardLayout->addLayout(topRow);
  auto path=new QLineEdit(card);path->setObjectName("savedCsvPath");path->hide();
  auto options=new QHBoxLayout;options->setSpacing(8);
  auto inputBase=new QComboBox,outputBase=new QComboBox,mode=new QComboBox,channel=new QComboBox;
  for(auto combo:{inputBase,outputBase}){combo->addItem("Hexadecimal",16);combo->addItem("Unsigned decimal",10);combo->addItem("Binary",2);combo->addItem("Octal",8);}
  inputBase->setObjectName("inputCsvBase");outputBase->setObjectName("outputCsvBase");mode->setObjectName("csvConversionMode");
  mode->addItems({"Raw word base","Engineering CSV from published rules"});channel->addItems({"External / CH1","Internal / CH2"});channel->setCurrentIndex(analysisChannel-1);
  for(auto combo:{inputBase,outputBase,mode,channel}){
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(8);
    combo->setMinimumWidth(0);
    combo->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
  }
  options->addWidget(new QLabel("Input base"));options->addWidget(inputBase,1);options->addWidget(new QLabel("Output base"));options->addWidget(outputBase,1);options->addWidget(new QLabel("Conversion"));options->addWidget(mode,2);options->addWidget(new QLabel("Source"));options->addWidget(channel,1);cardLayout->addLayout(options);
  const auto sources=recording.isEmpty()?QList<desktop::SessionCsvSource>{}:desktop::sessionCsvSources(recording);
  auto sessionFiles=new QComboBox(card);sessionFiles->setAccessibleName("Saved session CSV");
  for(const auto &source:sources)sessionFiles->addItem(QString("CH%1 · %2").arg(source.channel).arg(QFileInfo(source.path).fileName()));
  if(!sources.empty())cardLayout->insertWidget(1,sessionFiles);else sessionFiles->hide();
  std::vector<aero::Signal> mappings;for(const auto &s:backend.profile){
    const auto set=workspaceDocument["icd_set_id"].toString().toStdString();
    if(!set.empty()&&!s.icdSetId.empty()&&s.icdSetId!=set)continue;
    if(workspaceSubsystem.isEmpty()||(s.subsystem.empty()?QString("Unassigned"):QString::fromStdString(s.subsystem))==workspaceSubsystem)mappings.push_back(s);
  }
  auto confirm=new QCheckBox(QString("Use %1 published rules in %2; imported CSV capture provenance is unverified").arg(mappings.size()).arg(workspaceSubsystem.isEmpty()?"all subsystems":workspaceSubsystem));confirm->setObjectName("confirmCsvRules");confirm->hide();cardLayout->addWidget(confirm);
  auto noRulesHint=new QFrame(card);noRulesHint->setProperty("card",true);auto hintLay=new QHBoxLayout(noRulesHint);hintLay->setContentsMargins(10,8,10,8);
  auto hintLbl=new QLabel("No active signal mappings published yet. Define Word 0–31 rules first to export Engineering CSV.",noRulesHint);hintLbl->setWordWrap(true);
  auto openMapBtn=new QPushButton("Open Signal Mappings…",noRulesHint);desktop::actionIcon(openMapBtn,"edit");
  hintLay->addWidget(hintLbl,1);hintLay->addWidget(openMapBtn);noRulesHint->hide();cardLayout->addWidget(noRulesHint);
  connect(openMapBtn,&QPushButton::clicked,this,[this,parent]{if(auto d=qobject_cast<QDialog *>(parent))d->close();navigate(12);});
  connect(mode,&QComboBox::currentIndexChanged,dialog,[mode,noRulesHint,mappings](int idx){noRulesHint->setVisible(idx==1 && mappings.empty());});
  auto table=new QTableWidget(card);table->setObjectName("csvPreview");table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setWordWrap(false);table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);cardLayout->addWidget(table,1);
  auto emptyOverlay=new QWidget(table->viewport());
  auto emptyLay=new QVBoxLayout(emptyOverlay);emptyLay->setAlignment(Qt::AlignCenter);emptyLay->setSpacing(10);
  auto emptyIcon=new QLabel(emptyOverlay);
  emptyIcon->setFixedSize(56,56);emptyIcon->setAlignment(Qt::AlignCenter);
  emptyIcon->setStyleSheet("background:#eaf5f0;border-radius:12px;");
  {
    auto pm=QIcon(":/icons/file.svg").pixmap(QSize(28,28),devicePixelRatioF());
    QPainter p(&pm);p.setCompositionMode(QPainter::CompositionMode_SourceIn);p.fillRect(pm.rect(),QColor("#16804a"));p.end();
    emptyIcon->setPixmap(pm);
  }
  auto emptyTitle=new QLabel("Open a CSV file to view data",emptyOverlay);
  emptyTitle->setStyleSheet("font-size:17px;font-weight:700;color:#142d3a;");
  auto emptySub=new QLabel("The data will be displayed here with the selected conversion.",emptyOverlay);
  emptySub->setStyleSheet("font-size:13px;color:#526a5c;");
  auto emptyBrowse=new QPushButton("Open CSV",emptyOverlay);
  emptyBrowse->setProperty("primary",true);desktop::actionIcon(emptyBrowse,"folder",true);
  connect(emptyBrowse,&QPushButton::clicked,browse,&QPushButton::click);
  emptyLay->addStretch(1);
  emptyLay->addWidget(emptyIcon,0,Qt::AlignHCenter);
  emptyLay->addWidget(emptyTitle,0,Qt::AlignHCenter);
  emptyLay->addWidget(emptySub,0,Qt::AlignHCenter);
  emptyLay->addWidget(emptyBrowse,0,Qt::AlignHCenter);
  emptyLay->addStretch(1);
  auto vpLay=new QVBoxLayout(table->viewport());
  vpLay->setContentsMargins(0,0,0,0);
  vpLay->addWidget(emptyOverlay);
  layout->addWidget(card,1);
  auto footer=new QHBoxLayout;footer->setSpacing(8);auto previous=new QPushButton("Previous"),next=new QPushButton("Next 100"),preview=new QPushButton("Preview"),save=new QPushButton("Export"),cancel=new QPushButton("Cancel"),close=new QPushButton("Close");
  preview->setObjectName("previewCsv");save->setObjectName("convertCsv");cancel->setObjectName("cancelCsv");cancel->setEnabled(false);
  previous->setEnabled(false);next->setEnabled(false);cancel->hide();
  auto mapRow=new QPushButton("Map to ICD");mapRow->setObjectName("mapCsvRoute");mapRow->setEnabled(false);
  auto plot=new QPushButton("Plot");plot->setObjectName("plotCsvSelection");plot->setEnabled(false);
  desktop::actionIcon(preview,"file",false,15);
  desktop::actionIcon(plot,"chart",false,15);
  desktop::actionIcon(mapRow,"grid",false,15);
  desktop::actionIcon(save,"download",false,15);
  footer->addWidget(previous);footer->addWidget(next);footer->addWidget(cancel);footer->addStretch(1);
  for(auto b:{preview,plot,mapRow,save,close})footer->addWidget(b);layout->addLayout(footer);
  auto status=new QLabel("Choose a recorded .atd or session CSV (.csv) file to preview. Originals are never changed.");status->setObjectName("csvConversionStatus");status->setWordWrap(true);status->setStyleSheet("background:#eef6f1;border:1px solid #cde2d5;border-radius:8px;padding:6px 10px;font-size:12px;color:#1b432e;");layout->addWidget(status);
  auto page=std::make_shared<quint64>(0);auto generation=std::make_shared<quint64>(0);auto cancellation=std::make_shared<std::shared_ptr<std::atomic_bool>>(std::make_shared<std::atomic_bool>(false));
  auto run=std::make_shared<std::function<void(bool)>>();
  struct Preview {QString path;int radix{};quint64 offset{};bool compact{},generic{};QList<QStringList> raw;unsigned verifiedChannel{};};
  auto shown=std::make_shared<Preview>();
  connect(table,&QTableWidget::currentCellChanged,dialog,[plot,mapRow,mode,shown](int row,int,int,int){
    const bool validRow=!shown->generic&&row>=0&&row<shown->raw.size();
    plot->setEnabled(mode->currentIndex()==0&&validRow);
    mapRow->setEnabled(mode->currentIndex()==0&&validRow);
  });
  connect(mapRow,&QPushButton::clicked,dialog,[this,parent,table,shown,channel]{
    const int r=table->currentRow();
    if(r<0||r>=shown->raw.size())return;
    const auto &rawCols=shown->raw[r];
    PreFillMappingRoute route;route.valid=true;
    route.channel=shown->verifiedChannel ? int(shown->verifiedChannel) : (channel->currentIndex()+1);
    bool ok=false;
    route.rt=rawCols.value(4).trimmed().toInt(&ok);if(!ok)route.rt=1;
    route.sa=rawCols.value(5).trimmed().toInt(&ok);if(!ok)route.sa=1;
    const auto dir=rawCols.value(6).trimmed();
    route.tx=dir.contains("RT→BC") || dir.compare("Tx",Qt::CaseInsensitive)==0 || dir=="1";
    const auto wc=rawCols.value(7).trimmed().toInt(&ok);
    route.wordCount=ok&&wc>0?wc:32;
    const QString wStr=rawCols.value(8).trimmed();
    const auto parts=wStr.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
    for(int w=0;w<parts.size()&&w<32;++w){
      bool wOk=false;
      const auto cleaned=parts[w].startsWith("0x",Qt::CaseInsensitive)?parts[w].mid(2):parts[w];
      const unsigned val=cleaned.toUInt(&wOk,shown->radix==16||parts[w].startsWith("0x",Qt::CaseInsensitive)?16:shown->radix?shown->radix:16);
      if(wOk && val<=0xFFFFu){route.sampleWords[w]=std::uint16_t(val);route.hasSample=true;}
    }
    if(auto d=qobject_cast<QDialog *>(parent))d->close();
    openSignalMappingForRoute(route);
  });
  *run=[this,dialog,path,inputBase,outputBase,mode,channel,confirm,table,emptyOverlay,status,page,generation,cancellation,preview,save,previous,next,cancel,mappings,shown,plot,mapRow,sources](bool exporting){
    if(path->text().trimmed().isEmpty())return;
    if(exporting&&mode->currentIndex()==1&&(!confirm->isChecked()||mappings.empty())){status->setText("Define and publish active signals first, and confirm the mapping checkbox before engineering conversion.");return;}
    QString output;
    if(exporting){const QFileInfo input(path->text());output=QFileDialog::getSaveFileName(dialog,"Export a new CSV",input.dir().filePath(input.completeBaseName()+(mode->currentIndex()?"_engineering":"_base"+QString::number(outputBase->currentData().toInt()))+".csv"),"CSV (*.csv)");if(output.isEmpty())return;if(QFileInfo::exists(output)){status->setText("Choose a new filename. Existing files are preserved.");return;}}
    (*cancellation)->store(true);*cancellation=std::make_shared<std::atomic_bool>(false);const auto stopped=*cancellation;const auto token=++*generation;
    for(auto b:{preview,save,previous,next,plot,mapRow})b->setEnabled(false);cancel->setEnabled(true);status->setText(exporting?"Converting the full CSV in the background…":"Loading 100 preview rows…");
    auto watcher=new QFutureWatcher<CsvJobResult>(dialog);
    const auto input=path->text().trimmed();const auto radix=inputBase->currentData().toInt();const auto offset=*page*100;
    connect(watcher,&QFutureWatcher<CsvJobResult>::finished,dialog,[this,watcher,token,generation,table,emptyOverlay,status,preview,save,previous,next,cancel,page,exporting,output,input,radix,offset,shown,plot,mapRow,inputBase,channel]{auto result=watcher->result();watcher->deleteLater();if(token!=*generation)return;for(auto b:{preview,save,previous,next})b->setEnabled(true);cancel->setEnabled(false);previous->setEnabled(*page>0);next->setEnabled(result.more);if(!result.error.isEmpty()){status->setStyleSheet("background:#fef2f2;border:1px solid #fecaca;border-radius:8px;padding:6px 10px;font-size:12px;color:#991b1b;");status->setText(result.error);status->show();toast(result.error);return;}status->setStyleSheet("background:#eef6f1;border:1px solid #cde2d5;border-radius:8px;padding:6px 10px;font-size:12px;color:#1b432e;");if(result.cancelled){status->setText("Cancelled. No converted file was published.");return;}if(exporting){status->setText(QString("Exported %1 source rows to %2. Original file preserved.").arg(result.processed).arg(output));plot->setEnabled(!shown->generic&&!shown->raw.empty()&&table->currentRow()>=0);mapRow->setEnabled(!shown->generic&&!shown->raw.empty()&&table->currentRow()>=0);return;}if(result.verifiedChannel){inputBase->setCurrentIndex(inputBase->findData(result.radix));channel->setCurrentIndex(int(result.verifiedChannel)-1);}
      *shown={input,result.radix?result.radix:radix,offset,result.compact,result.generic,result.raw,result.verifiedChannel};table->clear();table->setColumnCount(result.header.size());table->setHorizontalHeaderLabels(result.header);table->setRowCount(result.rows.size());emptyOverlay->setVisible(result.rows.isEmpty());
      QStringList labels;for(int r=0;r<result.rows.size();++r){labels<<QString::number(offset+r+1);for(int c=0;c<result.header.size();++c){auto item=new QTableWidgetItem(result.rows[r].value(c));item->setToolTip(result.raw.value(r).value(c,item->text()));table->setItem(r,c,item);}}
      table->setVerticalHeaderLabels(labels);for(int c=0;c<result.header.size();++c)table->setColumnWidth(c,c==8?320:150);if(table->rowCount())table->setCurrentCell(0,0);
      if(result.rows.isEmpty()){status->setText(QString("No data rows found in %1.").arg(QFileInfo(input).fileName()));}
      else if(result.generic){status->setText(QString("Generic CSV · %1 columns · Rows %2–%3").arg(result.header.size()).arg(offset+1).arg(offset+result.rows.size()));}
      else {status->setText(QString("%1 columns · Rows %2–%3 · %4").arg(result.header.size()).arg(offset+1).arg(offset+result.rows.size()).arg(result.verifiedChannel?"Export hash verified; channel and input base loaded":"Stored time and ordering preserved; select input base for unprefixed words"));}});
    const auto metadata=std::find_if(sources.begin(),sources.end(),[&](const auto &s){return s.path==input;});
    const auto source=metadata==sources.end()?desktop::SessionCsvSource{}:*metadata;
    watcher->setFuture(QtConcurrent::run([input,output,exporting,inputRadix=radix,outputRadix=static_cast<aero::NumericRadix>(outputBase->currentData().toUInt()),engineering=mode->currentIndex()==1,selectedChannel=unsigned(channel->currentIndex()+1),offset,stopped,mappings,source]() mutable {
      CsvJobResult result;
      try {
        if(input.endsWith(".atd",Qt::CaseInsensitive)){
          aero::CsvExportOptions opts{aero::CsvLayout::Compact11,exporting?outputRadix:static_cast<aero::NumericRadix>(inputRadix)};
          result.compact=true;result.radix=int(opts.radix);
          result.header=engineering?QStringList{"source_row","card_time","channel","bus","signal_id","engineering_value","unit","quality"}:QString::fromStdString(aero::sessionCsvHeader(opts)).trimmed().split(',');
          QSaveFile destination(output);std::unique_ptr<QLockFile> lock;
          if(exporting){lock=std::make_unique<QLockFile>(output+".lock");lock->setStaleLockTime(0);if(!lock->tryLock(0)||QFileInfo::exists(output))throw std::runtime_error("Destination exists or another conversion is using it");if(!destination.open(QIODevice::WriteOnly))throw std::runtime_error(destination.errorString().toStdString());destination.write(desktop::csvLine(result.header));}
          std::array<std::optional<std::uint64_t>,2> previous;
          const auto scan=aero::scanRecording(std::filesystem::path(input.toStdWString()),[&](const aero::Record &record,std::uint64_t){
            if(stopped->load()){result.cancelled=true;return false;}
            const size_t chIdx=record.channel==2?1:0;
            const auto line=QByteArray::fromStdString(aero::sessionCsvRecord(record,previous[chIdx],opts));
            previous[chIdx]=record.timestampNs;
            ++result.processed;
            if(!exporting&&result.processed<=offset)return true;
            if(!exporting&&result.processed>offset+100){result.more=true;return false;}
            auto cells=desktop::csvCells(line);
            if(!exporting)result.raw<<cells;
            QList<QStringList> converted;
            if(!engineering)converted<<cells;
            else {
              const auto protocol=aero::interpret(record);bool matched=false;
              for(const auto &s:mappings){
                if(!aero::matchesSignalRoute(record,s,aero::signalCommand(protocol,s)))continue;
                if(s.sourceWord==aero::SignalWord::Payload && s.expectedWordCount &&
                   (protocol.mode || protocol.payloadCommand.count!=s.expectedWordCount))continue;
                matched=true;const auto value=aero::decodePayload(record,s,protocol,false);
                converted<<QStringList{QString::number(result.processed),cells[0],QString::number(record.channel),cells[4],QString::fromStdString(s.id),value?QString::number(*value,'g',17):QString(),QString::fromStdString(s.unit),value?"Decoded from recording":"No eligible decoded value; inspect mapping / message quality"};
              }
              if(!matched)converted<<QStringList{QString::number(result.processed),cells[0],QString::number(record.channel),cells[4],{},{},{},"No matching published mapping"};
            }
            for(const auto &row:converted)if(exporting){const auto encoded=desktop::csvLine(row);if(destination.write(encoded)!=encoded.size())throw std::runtime_error("CSV output write failed");}else result.rows<<row;
            return true;
          });
          if(result.cancelled)return result;
          if(!scan.error.empty()&&result.rows.isEmpty()&&!exporting)throw std::runtime_error(scan.error);
          if(exporting){if(QFileInfo::exists(output))throw std::runtime_error("Destination appeared during conversion; existing file preserved");if(!destination.commit())throw std::runtime_error("CSV output commit failed");}
          return result;
        }
        QFile file(input);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error(file.errorString().toStdString());
        if(!source.sha256.isEmpty()){
          QCryptographicHash hash(QCryptographicHash::Sha256);while(!file.atEnd()){if(stopped->load()){result.cancelled=true;return result;}const auto chunk=file.read(256*1024);if(chunk.isEmpty())break;hash.addData(chunk);}
          if(QString::fromLatin1(hash.result().toHex()).compare(source.sha256,Qt::CaseInsensitive)==0){inputRadix=int(source.options.radix);selectedChannel=source.channel;result.verifiedChannel=source.channel;}
          if(!file.seek(0))throw std::runtime_error("Cannot rewind CSV after verification");
        }
        std::unique_ptr<desktop::CsvInput> format;
        try {
          format=std::make_unique<desktop::CsvInput>(file);
        } catch(const std::exception &) {
          if(exporting)throw;
          if(!file.seek(0))throw;
          const auto firstRow=desktop::csvRow(file);
          if(firstRow.trimmed().isEmpty())throw std::runtime_error("CSV file is empty");
          result.generic=true;
          result.header=desktop::csvCells(firstRow);
          while(!file.atEnd()){
            if(stopped->load()){result.cancelled=true;return result;}
            const auto bytes=desktop::csvRow(file);if(bytes.trimmed().isEmpty())continue;++result.processed;
            if(result.processed<=offset)continue;
            if(result.processed>offset+100){result.more=true;break;}
            auto cells=desktop::csvCells(bytes);
            result.raw<<cells;result.rows<<cells;
          }
          return result;
        }
        result.compact=format->compact;result.radix=inputRadix;
        QSaveFile destination(output);std::unique_ptr<QLockFile> lock;
        if(exporting){lock=std::make_unique<QLockFile>(output+".lock");lock->setStaleLockTime(0);if(!lock->tryLock(0)||QFileInfo::exists(output))throw std::runtime_error("Destination exists or another conversion is using it");if(!destination.open(QIODevice::WriteOnly))throw std::runtime_error(destination.errorString().toStdString());}
        result.header=engineering?QStringList{"source_row","card_time","channel","bus","signal_id","engineering_value","unit","quality"}:format->header;
        if(exporting)destination.write(desktop::csvLine(result.header));
        while(!file.atEnd()){
          if(stopped->load()){result.cancelled=true;return result;}
          const auto bytes=desktop::csvRow(file);if(bytes.trimmed().isEmpty())continue;++result.processed;
          if(!exporting&&result.processed<=offset)continue;
          if(!exporting&&result.processed>offset+100){result.more=true;break;}
          auto cells=desktop::csvCells(bytes);format->validate(cells);if(!exporting)result.raw<<cells;QList<QStringList> converted;
          if(!engineering)converted<<format->convert(cells,inputRadix,outputRadix);
          else {
            const auto record=format->record(cells,inputRadix,selectedChannel,result.processed);bool matched=false;
            const auto protocol=aero::interpret(record);
            for(const auto &s:mappings){
              if(!aero::matchesSignalRoute(record,s,aero::signalCommand(protocol,s)))continue;
              if(s.sourceWord==aero::SignalWord::Payload && s.expectedWordCount &&
                 (protocol.mode || protocol.payloadCommand.count!=s.expectedWordCount))continue;
              matched=true;const auto value=aero::decodePayload(record,s,protocol,false);converted<<QStringList{QString::number(result.processed),cells[0],QString::number(selectedChannel),cells[4],QString::fromStdString(s.id),value?QString::number(*value,'g',17):QString(),QString::fromStdString(s.unit),value?"Decoded from CSV; capture provenance unverified":"No eligible decoded value; inspect mapping / message quality"};
            }
            if(!matched)converted<<QStringList{QString::number(result.processed),cells[0],QString::number(selectedChannel),cells[4],{},{},{},"No matching published mapping"};
          }
          for(const auto &row:converted)if(exporting){const auto encoded=desktop::csvLine(row);if(destination.write(encoded)!=encoded.size())throw std::runtime_error("CSV output write failed");}else result.rows<<row;
        }
        if(exporting){if(stopped->load()){result.cancelled=true;return result;}if(QFileInfo::exists(output))throw std::runtime_error("Destination appeared during conversion; existing file preserved");if(!destination.commit())throw std::runtime_error("CSV output commit failed");}
      }catch(const std::exception &e){result.error=QString("CSV row %1: %2").arg(result.processed).arg(QString::fromUtf8(e.what()));}
      return result;
    }));
  };
  connect(browse,&QPushButton::clicked,dialog,[this,dialog,path,page,run]{const auto file=QFileDialog::getOpenFileName(dialog,"Open recorded CSV or ATD",sessionDir,"CSV and Recordings (*.csv *.atd);;CSV (*.csv);;Recordings (*.atd)");if(file.isEmpty())return;path->setText(file);*page=0;(*run)(false);});
  connect(preview,&QPushButton::clicked,dialog,[run]{(*run)(false);});connect(save,&QPushButton::clicked,dialog,[run]{(*run)(true);});
  connect(previous,&QPushButton::clicked,dialog,[page,run]{if(*page)--*page;(*run)(false);});connect(next,&QPushButton::clicked,dialog,[page,run]{++*page;(*run)(false);});
  connect(mode,&QComboBox::currentIndexChanged,dialog,[confirm,outputBase,page,plot](int index){confirm->setVisible(index==1);outputBase->setEnabled(index==0);plot->setEnabled(false);*page=0;});
  connect(cancel,&QPushButton::clicked,dialog,[cancellation]{(*cancellation)->store(true);});connect(close,&QPushButton::clicked,dialog,[this,parent]{if(auto d=qobject_cast<QDialog *>(parent))d->close();else navigate(4);});connect(dialog,&QObject::destroyed,[cancellation]{(*cancellation)->store(true);});
  connect(sessionFiles,&QComboBox::currentIndexChanged,dialog,[path,page,run,sources](int i){if(i>=0&&i<sources.size()){path->setText(sources[i].path);*page=0;(*run)(false);}});
  connect(table,&QTableWidget::cellDoubleClicked,dialog,[dialog,table,shown](int row,int){if(row<0||row>=shown->raw.size())return;QStringList cells;for(int c=0;c<table->columnCount();++c)cells<<table->horizontalHeaderItem(c)->text()+": "+shown->raw[row].value(c);auto details=new QDialog(dialog);details->setAttribute(Qt::WA_DeleteOnClose);details->setWindowTitle(QString("Source row %1").arg(shown->offset+row+1));auto layout=new QVBoxLayout(details);auto text=new QPlainTextEdit(cells.join('\n'),details);text->setReadOnly(true);layout->addWidget(text);details->resize(640,420);details->show();});
  connect(plot,&QPushButton::clicked,dialog,[this,dialog,parent,path,table,shown,inputBase,channel,mappings,mode]{
    const auto row=table->currentRow();if(mode->currentIndex()!=0||row<0||row>=shown->raw.size()||path->text().trimmed()!=shown->path||inputBase->currentData().toInt()!=shown->radix){toast("Preview raw rows from this file and input base before plotting.");return;}
    const auto cells=shown->raw[row];
    try{
      auto commandCell=cells[6].trimmed();if(commandCell.isEmpty())commandCell=cells[5].trimmed();
      const auto command=aero::command(desktop::csvWord(commandCell.section(desktop::csvWhitespace(),0,0),shown->radix));
      const auto firstCommand=aero::command(desktop::csvWord(cells[5].trimmed().isEmpty()?commandCell.section(desktop::csvWhitespace(),0,0):cells[5].trimmed().section(desktop::csvWhitespace(),0,0),shown->radix));
      if(commandCell.isEmpty()||command.mode){toast("Select a data message row to plot.");return;}
      QDialog choose(dialog);choose.setObjectName("recordedCsvSelection");choose.setWindowTitle("Plot recorded CSV");auto form=new QFormLayout(&choose);
      auto stream=new QComboBox(&choose);stream->addItems({"Select channel","External / CH1","Internal / CH2"});
      stream->setObjectName("recordedCsvChannel");const auto name=QFileInfo(shown->path).completeBaseName();stream->setCurrentIndex(shown->verifiedChannel?int(shown->verifiedChannel):name.endsWith("_EXT",Qt::CaseInsensitive)?1:name.endsWith("_INT",Qt::CaseInsensitive)?2:0);
      form->addRow("Channel",stream);form->addRow("Route",new QLabel(QString("Bus %1 · RT %2 / SA %3 · %4").arg(cells[4]).arg(command.rt).arg(command.sa).arg(command.transmit?"Transmit":"Receive"),&choose));
      std::array<QComboBox *,4> picks{};
      for(int i=0;i<4;++i){auto combo=picks[i]=new QComboBox(&choose);combo->addItem("None");for(unsigned w=0;w<command.count;++w)combo->addItem(QString("Raw word %1").arg(w),int(w));for(int m=0;m<int(mappings.size());++m){const auto &s=mappings[m];const auto &route=s.sourceWord==aero::SignalWord::Command1||s.sourceWord==aero::SignalWord::Status2?firstCommand:command;if(s.rt==route.rt&&s.sa==route.sa&&s.transmit==route.transmit&&(!s.expectedWordCount||s.expectedWordCount==route.count)&&!s.simulationOnly)combo->addItem(QString::fromStdString(s.id+" · "+s.unit),32+m);}form->addRow(QString("Trace %1").arg(i+1),combo);}picks[0]->setCurrentIndex(1);
      auto first=new QLineEdit(&choose),last=new QLineEdit(&choose);form->addRow("Start card seconds (optional)",first);form->addRow("End card seconds (optional)",last);
      auto error=new QLabel(&choose);form->addRow(error);auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&choose);form->addRow(buttons);
      desktop::RecordedCsvRequest request;request.path=shown->path;request.radix=shown->radix;request.route=command;request.bus=cells[4]=="B";
      connect(buttons,&QDialogButtonBox::rejected,&choose,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,&choose,[&]{
        request.channel=stream->currentIndex();request.traces.clear();request.first.reset();request.last.reset();
        if(!request.channel){error->setText("Select the source channel.");return;}
        for(auto pair:{std::pair{first,&request.first},std::pair{last,&request.last}})if(!pair.first->text().trimmed().isEmpty()){
          bool ok=false;const auto value=pair.first->text().toDouble(&ok);
          if(!ok||!std::isfinite(value)||value<0||value>=double(UINT64_MAX)/1e9){error->setText("Enter valid nonnegative card seconds.");return;}*pair.second=std::uint64_t(value*1e9);
        }
        if(request.first&&request.last&&*request.last<*request.first){error->setText("End must follow start.");return;}
        for(auto combo:picks)if(combo->currentIndex()>0){const auto index=combo->currentData().toInt();desktop::CsvTrace trace;trace.title=combo->currentText();
          if(index<32)trace.word=index;else {trace.mapping=mappings.at(index-32);const auto &s=*trace.mapping;if((s.channel&&s.channel!=request.channel)||(s.bus>=0&&unsigned(s.bus)!=request.bus)){error->setText("A selected mapping belongs to another channel or bus.");return;}for(const auto &limit:backend.captureConfig.checkRules)if(limit.signalId==s.id){trace.lower=limit.minimum;trace.upper=limit.maximum;break;}}
          request.traces.push_back(std::move(trace));
        }
        if(request.traces.empty()){error->setText("Select at least one trace.");return;}choose.accept();
      });
      if(choose.exec()==QDialog::Accepted){recordedCsv=std::move(request);if(auto d=qobject_cast<QDialog *>(parent))d->close();navigate(7);}
    }catch(const std::exception &e){toast(QString::fromUtf8(e.what()));}
  });
  if(!sources.empty()){path->setText(sources.front().path);(*run)(false);}
  else if(!recording.isEmpty()&&QFileInfo(recording).isFile()){path->setText(recording);(*run)(false);}
  return dialog;
}

void Window::recordedCsvPage() {
  auto form=showForm<Ui::PlotsPage>();
  auto request=std::make_shared<desktop::RecordedCsvRequest>(*recordedCsv);
  auto graphResult=std::make_shared<desktop::RecordedCsvResult>();
  const std::array<Plot *,4> views{form.signalPlot0,form.signalPlot1,form.signalPlot2,form.signalPlot3};
  const std::array<QFrame *,4> cards{form.plotCard0,form.plotCard1,form.plotCard2,form.plotCard3};
  const std::array<QWidget *,4> controls{form.panel0Controls,form.panel1Controls,form.panel2Controls,form.panel3Controls};
  form.bodyLayout->insertWidget(0,form.actionBar);
  for(auto widget:{static_cast<QWidget *>(form.plotSource),static_cast<QWidget *>(form.plotSignal),static_cast<QWidget *>(form.pausePlotsButton),static_cast<QWidget *>(form.addSignalButton),static_cast<QWidget *>(form.cursorInspectButton)})widget->hide();
  auto source=new QLabel(QString("Recorded CSV · %1 · CH%2/%3 · RT %4 / SA %5 · %6").arg(QFileInfo(request->path).fileName()).arg(request->channel).arg(request->bus?"B":"A").arg(request->route.rt).arg(request->route.sa).arg(request->route.transmit?"Transmit":"Receive"),content);
  source->setToolTip(request->path);source->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);form.actionBarLayout->insertWidget(0,source,1);
  auto exit=new QPushButton("Return to live / replay",content);form.actionBarLayout->addWidget(exit);
  auto open=new QPushButton("CSV Viewer",content);form.actionBarLayout->addWidget(open);
  connect(exit,&QPushButton::clicked,content,[this]{recordedCsv.reset();navigate(7);});
  connect(open,&QPushButton::clicked,content,[this]{csvTools();});
  connect(form.exportPlotButton,&QPushButton::clicked,content,[this]{exportPlot();});
  auto range=new QWidget(content);auto row=new QHBoxLayout(range);row->setContentsMargins(0,0,0,0);
  auto start=new QLineEdit(range),end=new QLineEdit(range);start->setPlaceholderText("Start (card seconds)");end->setPlaceholderText("End (card seconds)");start->setAccessibleName("Start card seconds");end->setAccessibleName("End card seconds");
  if(request->first)start->setText(QString::number(*request->first/1e9,'g',17));if(request->last)end->setText(QString::number(*request->last/1e9,'g',17));
  auto rescan=new QPushButton("Scan range",range),cancel=new QPushButton("Cancel scan",range),statsButton=new QPushButton("Statistics",range),rangeToggle=new QPushButton("Range",range);
  statsButton->setCheckable(true);rangeToggle->setCheckable(true);
  row->addWidget(start);row->addWidget(end);row->addWidget(rescan);
  form.actionBarLayout->addWidget(rangeToggle);form.actionBarLayout->addWidget(cancel);form.actionBarLayout->addWidget(statsButton);cancel->setEnabled(false);cancel->hide();range->hide();
  connect(rangeToggle,&QPushButton::toggled,range,&QWidget::setVisible);
  form.bodyLayout->insertWidget(1,range);
  auto status=new QLabel("CSV source; capture provenance is not verified.",content);status->setObjectName("recordedCsvStatus");status->setWordWrap(true);status->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);status->setMaximumHeight(42);form.bodyLayout->addWidget(status);
  auto stats=new QLabel(content);stats->setObjectName("recordedCsvStatistics");stats->setWordWrap(true);stats->hide();form.bodyLayout->addWidget(stats);
  connect(statsButton,&QPushButton::toggled,stats,&QLabel::setVisible);
  auto selected=std::make_shared<size_t>(0);
  auto cursor=std::make_shared<std::optional<desktop::RecordedCsvPoint>>();
  for(size_t i=0;i<views.size();++i){
    cards[i]->setVisible(i==0);controls[i]->hide();
    if(i>=request->traces.size())continue;
    auto plot=views[i];plots<<plot;plot->setTimeAxis(true);
    plot->onPick=[i,selected,cursor,form,graphResult,stats](QPointF query){
      if(i>=graphResult->series.size())return;
      const auto &series=graphResult->series[i];
      if(*selected!=i){cursor->reset();form.cursorB->setText("B: select sample");form.cursorDelta->setText("Δt / ΔY");}
      *selected=i;stats->setText(series.statistics.text());
      const desktop::RecordedCsvPoint *nearest=nullptr;
      for(const auto &point:series.points)if(point.row&&(!nearest||std::abs(point.point.x()-query.x())<std::abs(nearest->point.x()-query.x())||(point.point.x()==nearest->point.x()&&std::abs(point.point.y()-query.y())<std::abs(nearest->point.y()-query.y()))))nearest=&point;
      if(!nearest)return;
      const auto label=QString("Row %1 · %2 s · %3").arg(nearest->row).arg(desktop::displayValue(nearest->point.x()),desktop::displayValue(nearest->point.y()));
      if(!*cursor){*cursor=*nearest;form.cursorA->setText("A: "+label);}
      else{form.cursorB->setText("B: "+label);form.cursorDelta->setText(QString("Δt %1 s · ΔY %2").arg(desktop::displayValue(nearest->point.x()-(*cursor)->point.x()),desktop::displayValue(nearest->point.y()-(*cursor)->point.y())));cursor->reset();}
    };
  }
  form.restorePlotsButton->setVisible(request->traces.size()>1);form.restorePlotsButton->setCheckable(true);
  connect(form.restorePlotsButton,&QPushButton::toggled,content,[cards,request](bool compare){for(size_t i=1;i<cards.size();++i)cards[i]->setVisible(compare&&i<request->traces.size());});
  auto stopped=std::make_shared<std::atomic_bool>();
  connect(cancel,&QPushButton::clicked,content,[stopped]{stopped->store(true);});
  connect(content,&QObject::destroyed,this,[stopped]{stopped->store(true);});
  const auto scan=[this,request,views,form,status,stats,start,end,rescan,cancel,graphResult,selected,cursor,stopped]{
    auto task=*request;task.first.reset();task.last.reset();
    for(auto pair:{std::pair{start,&task.first},std::pair{end,&task.last}})if(!pair.first->text().trimmed().isEmpty()){
      bool ok=false;const auto value=pair.first->text().toDouble(&ok);
      if(!ok||!std::isfinite(value)||value<0||value>=double(UINT64_MAX)/1e9){status->setText("Enter valid nonnegative card seconds.");return;}*pair.second=std::uint64_t(value*1e9);
    }
    if(task.first&&task.last&&*task.first>*task.last){status->setText("End must follow start.");return;}
    *request=task;recordedCsv=task;
    stopped->store(false);rescan->setEnabled(false);cancel->setEnabled(true);cancel->show();status->show();status->setText("Scanning recorded CSV…");
    auto watcher=new QFutureWatcher<desktop::RecordedCsvResult>(content);
    connect(watcher,&QFutureWatcher<desktop::RecordedCsvResult>::finished,content,[watcher,views,form,status,stats,rescan,cancel,request,graphResult,selected,cursor]{
      auto result=watcher->result();watcher->deleteLater();rescan->setEnabled(true);cancel->setEnabled(false);cancel->hide();
      if(result.cancelled||!result.error.isEmpty()){status->setText(result.cancelled?"Scan cancelled; previous plot retained.":result.error);return;}
      *graphResult=std::move(result);cursor->reset();form.cursorA->setText("A: select sample");form.cursorB->setText("B: select sample");form.cursorDelta->setText("Δt / ΔY");
      for(size_t i=0;i<graphResult->series.size();++i){
        QVector<QPointF> points;for(const auto &point:graphResult->series[i].points)points<<point.point;
        const auto &trace=request->traces[i];views[i]->setSeries(std::move(points),{},trace.lower,trace.upper);views[i]->resetView();
        views[i]->setStatus(trace.title+QString(" · %1 valid samples").arg(graphResult->series[i].statistics.count));
      }
      stats->setText(graphResult->series[*selected].statistics.text());
      status->setText(graphResult->omittedSegments?"Display limit reached. Narrow the scanned range to inspect omitted segments. Statistics include all matching rows.":"Recorded CSV · statistics cover the scanned range · cursor rows reference the original file");
      status->setVisible(graphResult->omittedSegments);
    });
    watcher->setFuture(QtConcurrent::run([task,stopped]{return desktop::readRecordedCsv(task,*stopped);}));
  };
  connect(rescan,&QPushButton::clicked,content,scan);
  configureAnalysisTools(true);
  if(auto tools=content->findChild<QWidget *>("analysisToolbar"))if(auto menu=tools->findChild<QMenu *>()){
    auto rangeAction=menu->addAction("Scan range…");rangeAction->setCheckable(true);connect(rangeAction,&QAction::toggled,range,&QWidget::setVisible);
  }
  scan();
}
