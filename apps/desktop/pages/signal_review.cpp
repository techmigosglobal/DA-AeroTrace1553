#include "main_window.hpp"
#include "services/profile_io.hpp"
#include "ui_signal_review.h"
#include "example_simulation.hpp"

namespace {
enum Column { Id, Rt, Sa, Direction, Count, Word, Lsb, Width, Type, Signed, Order,
              Scale, Offset, Unit, Channel, Bus, Reference, Result };
const QStringList headers{"id","rt","sa","direction","word_count","data_word","lsb","width",
  "representation","signed","word_order","scale","offset","unit","channel","bus","source_reference"};
constexpr int rowLimit=512;

QString reviewOverridesPath() {
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/Documents/review-edits.ini";
}

QString reviewOverrideKey(const QJsonObject &row) {
  const auto mapping=row["mapping"].toObject();
  const auto hash=mapping["source_hash"].toString();
  static const QRegularExpression digest(QStringLiteral("^[0-9a-f]{64}$"));
  if(!row.contains("pdf") || !digest.match(hash).hasMatch())return {};
  const QJsonArray identity{mapping["id"],row["page"],row["evidence"]};
  const auto key=QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex();
  return hash+"/"+QString::fromLatin1(key);
}

QString rowGroup(const QJsonObject &data) {
  return data["subsystem"].toString(data["mapping"].toObject()["subsystem"].toString("Unassigned"));
}
QString presetKey(const QJsonObject &data) {
  const auto mapping=data["mapping"].toObject();
  const auto name=data["icd_set"].toString(mapping["source_document"].toString());
  return "presets/"+QString::fromLatin1(QCryptographicHash::hash((name+"/"+rowGroup(data)).toUtf8(),QCryptographicHash::Sha256).toHex());
}
QString calibrationLayout(const QJsonArray &cells) {
  QJsonArray layout;
  for(const auto column:{Rt,Sa,Direction,Count,Width,Type,Unit})layout.append(cells[column]);
  return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(layout).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
}

QString stableRowKey(const QJsonObject &data) {
  const auto mapping=data["mapping"].toObject();
  auto identity=mapping["id"].toString();
  if(!mapping["subsystem"].toString().isEmpty() && !mapping["message_id"].toString().isEmpty() && !mapping["source_signal_id"].toString().isEmpty())
    identity=mapping["subsystem"].toString()+" / "+mapping["message_id"].toString()+" / "+mapping["source_signal_id"].toString();
  return "definitions/"+QString::fromLatin1(QCryptographicHash::hash((presetKey(data)+"/"+identity).toUtf8(),QCryptographicHash::Sha256).toHex());
}

class SignalChoices final : public QStyledItemDelegate {
  QStringList values;
public:
  SignalChoices(QStringList options,QObject *parent):QStyledItemDelegate(parent),values(std::move(options)){}
  QWidget *createEditor(QWidget *parent,const QStyleOptionViewItem &,const QModelIndex &) const override {
    auto editor=new QComboBox(parent);
    editor->addItem("Choose…",QString());
    for(const auto &value:values)editor->addItem(value,value);
    connect(editor,&QComboBox::activated,this,[this,editor]{
      auto delegate=const_cast<SignalChoices *>(this);
      emit delegate->commitData(editor);
      emit delegate->closeEditor(editor);
    });
    return editor;
  }
  void setEditorData(QWidget *widget,const QModelIndex &index) const override {
    auto editor=static_cast<QComboBox *>(widget);
    const auto current=index.data(Qt::EditRole).toString().trimmed();
    auto match=editor->findData(current);
    if(match<0 && !current.isEmpty()) {
      editor->addItem(current,current);
      match=editor->count()-1;
    }
    editor->setCurrentIndex(std::max(0,match));
  }
  void setModelData(QWidget *widget,QAbstractItemModel *model,const QModelIndex &index) const override {
    model->setData(index,static_cast<QComboBox *>(widget)->currentData(),Qt::EditRole);
  }
};


QJsonObject reviewRow(const QJsonObject &mapping) {
  QStringList cells;
  const auto text=[&](const char *key){return mapping.value(key).toVariant().toString();};
  cells << text("id") << text("rt") << text("sa") << (mapping["transmit"].toBool()?"transmit":"receive")
        << QString::number(mapping["expected_word_count"].toInt())
        << QString::number(mapping["word"].toInt()+1) << text("lsb") << text("width")
        << mapping["numeric_type"].toString("integer") << (mapping["signed"].toBool()?"true":"false")
        << (mapping["lsw_first"].toBool()?"lsw-first":"msw-first") << QString::number(mapping["scale"].toDouble(),'g',17) << QString::number(mapping["offset"].toDouble(),'g',17)
        << (text("unit").isEmpty()?QString("unitless"):text("unit")) << text("channel")
        << (mapping["bus"].toInt(-1)<0?"any":mapping["bus"].toInt()==0?"A":"B") << text("source_document");
  return {{"cells",QJsonArray::fromStringList(cells)},{"mapping",mapping}};
}
QString parseCsv(QStringView csv, QJsonArray &result) {
  QList<QStringList> records;
  QStringList row;
  QString cell;
  bool quoted=false, closed=false;
  for(qsizetype i=0;i<csv.size();++i) {
    const auto c=csv[i];
    if(quoted) {
      if(c!='"')cell+=c;
      else if(i+1<csv.size() && csv[i+1]=='"') {cell+='"';++i;}
      else {quoted=false;closed=true;}
    } else if(c==',' || c=='\n' || c=='\r') {
      row.append(cell);cell.clear();closed=false;
      if(row.size()>headers.size())return "Too many CSV columns.";
      if(c!=',') {
        if(c=='\r' && i+1<csv.size() && csv[i+1]=='\n')++i;
        if(row.size()>1 || !row[0].trimmed().isEmpty())records.append(row);
        if(records.size()>rowLimit+1)return "CSV exceeds 512 rows.";
        row.clear();
      }
    } else if(c=='"' && cell.isEmpty() && !closed)quoted=true;
    else if(closed || c=='"')return "Malformed quoted CSV field.";
    else cell+=c;
  }
  if(quoted)return "Unclosed quoted CSV field.";
  row.append(cell);
  if(row.size()>1 || !row[0].trimmed().isEmpty())records.append(row);
  if(records.size()<2 || records.size()>rowLimit+1)return "Paste a header and 1–512 data rows.";
  auto keys=records.takeFirst();
  for(auto &key:keys){key.remove(QChar::ByteOrderMark);key=key.trimmed().toLower();}
  QSet<QString> seen;
  for(const auto &key:keys){if(!headers.contains(key) || seen.contains(key))return "Unknown or duplicate CSV column: "+key;seen.insert(key);}
  for(int i=0;i<=Unit;++i)if(!seen.contains(headers[i]))return "Missing CSV column: "+headers[i];
  for(const auto &fields:records) {
    if(fields.size()!=keys.size())return "CSV row "+QString::number(result.size()+2)+" has the wrong number of cells.";
    QJsonArray cells;for(const auto &key:headers)cells.append(fields.value(keys.indexOf(key)));
    result.append(QJsonObject{{"cells",cells}});
  }
  return {};
}
QString mappingAt(QTableWidget *grid, int row, aero::Signal &signal) {
  if(row<0)return "Select a signal row.";
  auto mapping=grid->item(row,Id)->data(Qt::UserRole).toJsonObject()["mapping"].toObject();
  const auto cell=[&](int column){return grid->item(row,column)->text().trimmed();};
  const auto required={Id,Rt,Sa,Direction,Count,Word,Lsb,Width,Type,Signed,Order,Scale,Offset,Unit};
  for(const auto column:required)if(cell(column).isEmpty())return "Complete "+headers[column]+".";
  for(const auto column:{Rt,Sa,Count,Word,Lsb,Width,Scale,Offset}) {
    bool ok=false;auto number=cell(column).toDouble(&ok);
    if(!ok || !std::isfinite(number))return "Invalid "+headers[column]+".";
    const auto key=column==Count?QString("expected_word_count"):column==Word?QString("word"):headers[column];
    mapping[key]=column==Word?number-1:number;
  }
  const auto direction=cell(Direction).toLower(), sign=cell(Signed).toLower(), order=cell(Order).toLower();
  if(direction!="receive" && direction!="transmit")return "Direction must be receive or transmit.";
  if(sign!="true" && sign!="false")return "Signed must be true or false.";
  if(order!="msw-first" && order!="lsw-first")return "Use msw-first or lsw-first.";
  const auto channel=cell(Channel).toLower(), bus=cell(Bus).toUpper();
  if(!channel.isEmpty() && channel!="any" && channel!="0" && channel!="1" && channel!="2")return "Channel must be any, 1, or 2.";
  if(!bus.isEmpty() && bus!="ANY" && bus!="A" && bus!="B")return "Bus must be any, A, or B.";
  const auto type=cell(Type).toLower();
  if(type!="integer" && sign=="true")return "Signed applies to two’s-complement integer fields.";
  mapping["id"]=cell(Id);mapping["numeric_type"]=type;mapping["signed"]=sign=="true";
  mapping["transmit"]=direction=="transmit";mapping["lsw_first"]=order=="lsw-first";
  mapping["unit"]=cell(Unit).compare("unitless",Qt::CaseInsensitive)==0?QString():cell(Unit);
  mapping["channel"]=channel.toInt();mapping["bus"]=bus=="A"?0:bus=="B"?1:-1;
  mapping["source_document"]=cell(Reference);
  return desktop::signalFromJson(mapping,signal);
}
bool overlaps(const aero::Signal &a,const aero::Signal &b) {
  if(a.rt!=b.rt || a.sa!=b.sa || a.transmit!=b.transmit ||
     (a.channel && b.channel && a.channel!=b.channel) || (a.bus>=0 && b.bus>=0 && a.bus!=b.bus) ||
     (a.expectedWordCount && b.expectedWordCount && a.expectedWordCount!=b.expectedWordCount) ||
     a.simulationOnly!=b.simulationOnly)return false;
  const auto mask=[](const aero::Signal &s,unsigned word) {
    const auto count=(s.lsb+s.width+15)/16;
    if(word<s.word || word>=s.word+count)return 0u;
    const auto order=s.leastSignificantWordFirst?word-s.word:count-1-(word-s.word);
    unsigned mask=0;
    for(unsigned bit=0;bit<16;++bit)if(order*16+bit>=s.lsb && order*16+bit<s.lsb+s.width)mask|=1u<<bit;
    return mask;
  };
  for(unsigned word=std::max(a.word,b.word);word<std::min(a.word+4,b.word+4);++word)
    if(mask(a,word)&mask(b,word))return true;
  return false;
}
}

void Window::reviewDocument(const QJsonObject &document,const QString &pdf,const QJsonArray &checked) {
  QJsonArray rows;
  QSettings edits(reviewOverridesPath(),QSettings::IniFormat);
  const auto pdfFor=[&pdf](const QJsonValue &source) {
    static const QRegularExpression digest(QStringLiteral("^[0-9a-f]{64}$"));
    const auto hash=source.toString();
    return !pdf.isEmpty() && digest.match(hash).hasMatch()?QFileInfo(pdf).dir().filePath(hash+".pdf"):pdf;
  };
  const auto stage=[this,&rows,&edits,setName=document["document"].toString()](QJsonObject row) {
    row["icd_set"]=setName;
    const auto mapping=row["mapping"].toObject();
    const auto source=mapping["source_hash"].toString().toStdString();
    const auto id=mapping["id"].toString().toStdString();
    const auto key=reviewOverrideKey(row);
    const auto saved=key.isEmpty()?QJsonArray():QJsonDocument::fromJson(edits.value(key).toByteArray()).array();
    const auto seeded=row["cells"].toArray();
    const auto documented=row["source_cells"].toArray();
    const auto imported=documented.size()==headers.size()?documented:seeded;row["import_cells"]=imported;
    auto cells=seeded;
    const auto preset=QJsonDocument::fromJson(edits.value(presetKey(row)).toByteArray()).object();
    for(const auto &column:{Order,Scale,Offset,Channel}) {
      const bool compatible=column!=Scale && column!=Offset || preset["compatible_layouts"].toArray().contains(calibrationLayout(imported));
      if(compatible && cells[column].toString().isEmpty() && preset.contains(headers[column]))cells[column]=preset[headers[column]];
    }
    const auto previous=QJsonDocument::fromJson(edits.value(stableRowKey(row)).toByteArray()).object();
    const auto before=previous["original"].toArray(),manual=previous["cells"].toArray();
    if(before.size()==headers.size() && manual.size()==headers.size()) {
      bool changed=false;
      for(int column=0;column<=Bus;++column)changed|=before[column]!=imported[column];
      row["definition_changed"]=changed;
      for(int column=0;column<headers.size();++column)if(manual[column]!=before[column])cells[column]=manual[column];
    }
    if(saved.size()==headers.size())cells=saved;
    const auto prior=std::find_if(backend.profile.begin(),backend.profile.end(),[&](const auto &signal){return signal.id==id || signal.id==cells[Id].toString().toStdString();});
    if(prior!=backend.profile.end() && prior->sourceHash!=source) {
      const auto priorCells=reviewRow(desktop::signalToJson(*prior))["cells"].toArray();
      if(before.size()!=headers.size()) {
        bool changed=false;
        for(int column=Rt;column<=Bus;++column)
          if(!imported[column].toString().isEmpty())changed|=imported[column]!=priorCells[column];
        row["definition_changed"]=changed;
      }
      if(QString::fromStdString(prior->sourceDocument)!=mapping["source_document"].toString())row["definition_changed"]=true;
      if(calibrationLayout(imported)==calibrationLayout(priorCells))
        for(const auto column:{Order,Scale,Offset,Channel,Bus})if(cells[column].toString().isEmpty() || before.size()!=headers.size())cells[column]=priorCells[column];
    }
    row["cells"]=cells;
    const auto effectiveId=row["cells"].toArray().at(Id).toString().toStdString();
    if(!source.empty() && std::any_of(backend.profile.begin(),backend.profile.end(),[&](const auto &signal){
      return signal.sourceHash==source && (signal.id==id || signal.id==effectiveId);
    }))return;
    row["override_key"]=key;
    rows.append(row);
  };
  const auto candidates=checked.isEmpty()?document["signals"].toArray():checked;
  for(const auto &item:candidates) {
    const auto mapping=item.toObject();
    auto row=reviewRow(mapping);
    row["pdf"]=pdfFor(mapping["source_hash"]);row["page"]=mapping["source_page"];row["evidence"]=mapping["evidence"].toObject()["text"];
    stage(row);
  }
  for(const auto &item:document["review"].toArray()) {
    const auto evidence=item.toObject(), field=evidence["draft_field"].toObject();
    const auto explicitCells=evidence["draft_cells"].toObject();
    if(field.isEmpty() && explicitCells.isEmpty())continue;
    const auto message=field["message"].toObject();
    const auto name=evidence["source_document"].toString(document["document"].toString());
    QStringList cells;
    for(int column=0;column<headers.size();++column)cells.append(QString());
    cells[Rt]=QString::number(message["rt"].toInt());
    cells[Sa]=QString::number(message["sa"].toInt());
    cells[Direction]=message["direction"].toString();
    cells[Count]=QString::number(message["word_count"].toInt());
    cells[Word]=QString::number(field["word_first"].toInt());
    const auto explicitFields=field["explicit"].toObject();
    const auto documentedId=explicitCells["id"].toString(explicitFields["id"].toString());
    cells[Id]=documentedId.isEmpty() && !evidence["needs_identity"].toBool()?QString("RT%1-SA%2-%3-W%4-%5-B%6")
      .arg(cells[Rt],cells[Sa],cells[Direction],cells[Word])
      .arg(field["word_last"].toInt()).arg(explicitFields["lsb"].toInt(0)):documentedId;
    cells[Unit]=field["document_unit"].toString();
    for(const auto &[column,key]:std::initializer_list<std::pair<int,const char *>>{
      {Lsb,"lsb"},{Width,"width"},{Type,"representation"},{Signed,"signed"},
      {Order,"order"},{Scale,"scale"},{Offset,"offset"}}) {
      if(explicitFields.contains(key)) cells[column]=explicitFields[key].toVariant().toString();
    }
    if(!explicitCells.isEmpty()) {
      for(int column=0;column<headers.size();++column)
        cells[column]=explicitCells.value(headers[column]).toVariant().toString();
      if(cells[Count].isEmpty())cells[Count]="0";
    }
    cells[Reference]=name;
    QJsonObject mapping{{"id",cells[Id]},{"source_document",name},{"source_hash",evidence["source_hash"]},{"source_page",evidence["page"]},
      {"subsystem",evidence["subsystem"]},{"message_id",evidence["message_id"]},{"source_signal_id",evidence["source_signal_id"]},{"icd_set_id",evidence["icd_set_id"]},{"icd_revision",evidence["icd_revision"]}};
    const auto documentedCells=QJsonArray::fromStringList(cells);
    if(evidence["source_hash"].toString()=="37e09d13f2863af4ab25507a14bbcc5fdcae8a1a176242153972a40225604937") {
      if(cells[Type].startsWith("float") && cells[Order].isEmpty())cells[Order]="msw-first";
      if(cells[Channel].isEmpty()) {
        if(evidence["subsystem"].toString()=="Subsystem-1")cells[Channel]="1";
        if(evidence["subsystem"].toString()=="Subsystem-3")cells[Channel]="2";
      }
    }
    stage(QJsonObject{{"cells",QJsonArray::fromStringList(cells)},{"source_cells",documentedCells},{"mapping",mapping},{"pdf",pdfFor(evidence["source_hash"])},{"page",evidence["page"]},
      {"evidence",evidence["text"].toString()+"\n"+evidence["reason"].toString()+(documentedId.isEmpty()?QString():"\nSignal ID extracted from the document.")},
      {"draft_field",field},{"table_icd",evidence["table_icd"]},{"subsystem",evidence["subsystem"]}});
  }
  if(rows.isEmpty()){reviewSignals();return;}
  reviewSignals(rows);
}

void Window::reviewSignals(QJsonArray rows,const QString &csv,bool replace) {
  if(backend.replaySource){toast("Replay uses its recorded profile. Return to the live workspace before reviewing mappings.");return;}
  if(signalReviewDialog) {signalReviewDialog->raise();return;}
  auto dialog=new QDialog(this);
  signalReviewDialog=dialog;
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowModality(Qt::WindowModal);
  Ui::SignalReview form;form.setupUi(dialog);
  auto grid=form.signalGrid;
  grid->setColumnCount(Result+1);
  grid->setHorizontalHeaderLabels({"Signal ID","RT","Subaddress","Direction","Word count","Data word","LSB","Width",
    "Representation","Signed","Word order","Scale","Offset","Unit","Channel","Bus","ICD / revision","Review result"});
  grid->horizontalHeaderItem(Word)->setToolTip("One-based data word number");
  grid->horizontalHeaderItem(Lsb)->setToolTip("Zero-based bit offset from the least significant bit");
  const auto choices=[grid](int column,QStringList options){
    grid->setItemDelegateForColumn(column,new SignalChoices(std::move(options),grid));
  };
  choices(Direction,{"receive","transmit"});
  choices(Type,{"integer","float32","float64","bcd"});
  choices(Signed,{"true","false"});
  choices(Order,{"msw-first","lsw-first"});
  choices(Channel,{"any","1","2"});
  choices(Bus,{"any","A","B"});
  form.reviewStatus->setProperty("tone","notice");
  grid->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  grid->horizontalHeader()->setDefaultSectionSize(105);
  grid->setColumnWidth(Id,180);grid->setColumnWidth(Result,280);
  const auto insert=[grid](int row,const QJsonObject &data){
    if(row==grid->rowCount())grid->insertRow(row);
    const auto cells=data["cells"].toArray();
    for(int column=0;column<=Result;++column) {
      auto item=new QTableWidgetItem(column<cells.size()?cells.at(column).toString():QString());
      if(data.contains("pdf") && column<=Unit && item->text().isEmpty()) {
        item->setBackground(QColor("#fff0d2"));
        item->setToolTip("Review this missing field");
      }
      if(column==Id)item->setData(Qt::UserRole,data);
      if(column==Result)item->setFlags(item->flags()&~Qt::ItemIsEditable);
      grid->setItem(row,column,item);
    }
  };
  const auto append=[grid,form,insert](const QJsonArray &data) {
    if(grid->rowCount()+data.size()>rowLimit){form.reviewStatus->setText("Maximum 512 signals. Remove rows before adding more.");return false;}
    for(const auto &row:data) {const auto object=row.toObject();insert(grid->rowCount(),object.contains("id")?reviewRow(object):object);}
    if(grid->currentRow()<0 && grid->rowCount())grid->setCurrentCell(0,0);
    return true;
  };
  const auto &base=backend.profile;
  QSet<QString> stagedIds;
  for(const auto &entry:rows){const auto row=entry.toObject();stagedIds.insert(row["id"].toString());stagedIds.insert(row["mapping"].toObject()["id"].toString());const auto cells=row["cells"].toArray();if(!cells.empty())stagedIds.insert(cells[Id].toString());}
  QSet<QString> documentIds;
  QJsonArray documented=workspaceDocument["signals"].toArray();for(const auto &entry:workspaceDocument["review"].toArray())documented.append(entry);
  for(const auto &entry:documented)documentIds.insert(entry.toObject()["id"].toString());
  QSettings priorEdits(reviewOverridesPath(),QSettings::IniFormat);
  if(!replace)for(const auto &entry:desktop::profileToJson(base)["signals"].toArray())if(!stagedIds.contains(entry.toObject()["id"].toString())) {
    const auto mapping=entry.toObject();auto data=reviewRow(mapping);
    const auto originalId=mapping["subsystem"].toString()+" / "+mapping["message_id"].toString()+" / "+mapping["source_signal_id"].toString();
    if(documentIds.contains(mapping["id"].toString()) || documentIds.contains(originalId))data["icd_set"]=workspaceDocument["document"];
    if(!mapping["source_hash"].toString().isEmpty()) {
      data["pdf"]=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/Documents/"+mapping["source_hash"].toString()+".pdf";
      data["page"]=mapping["source_page"];data["evidence"]="Published mapping. Check the original PDF page.";
      data["override_key"]=reviewOverrideKey(data);
      const auto saved=QJsonDocument::fromJson(priorEdits.value(stableRowKey(data)).toByteArray()).object()["original"].toArray();
      data["import_cells"]=saved.size()==headers.size()?saved:data["cells"].toArray();
    }
    insert(grid->rowCount(),data);
  }
  append(rows);
  const auto paste=[append,form](const QString &text) {
    QJsonArray entries;
    if(text.size()>4*1024*1024){form.reviewStatus->setText("CSV exceeds 4 MB.");return;}
    if(auto error=parseCsv(text,entries);!error.isEmpty()){form.reviewStatus->setText(error);return;}
    if(append(entries))form.reviewStatus->setText(QString("Added %1 rows for review.").arg(entries.size()));
  };
  connect(form.pasteCsv,&QPushButton::clicked,dialog,[paste]{paste(QApplication::clipboard()->text());});
  if(!csv.isEmpty())paste(csv);
  connect(form.copyTemplate,&QPushButton::clicked,dialog,[form]{
    QApplication::clipboard()->setText(headers.join(',')+"\n");
    form.reviewStatus->setText("CSV header copied. Enter rows and paste the complete CSV here.");
  });
  connect(form.addRow,&QPushButton::clicked,dialog,[append,grid]{
    if(append(QJsonArray{QJsonObject{}})){grid->setCurrentCell(grid->rowCount()-1,Id);grid->setFocus();}
  });
  connect(form.removeRow,&QPushButton::clicked,dialog,[grid]{if(grid->currentRow()>=0)grid->removeRow(grid->currentRow());});
  const auto selected=[grid]{return grid->currentRow()<0 || !grid->item(grid->currentRow(),Id)?QJsonObject():grid->item(grid->currentRow(),Id)->data(Qt::UserRole).toJsonObject();};
  const auto refreshEvidence=[form,selected]{
    const auto row=selected();
    const auto page=row["page"].toInt();
    form.sourceEvidence->setPlainText((page>0?QString("PDF page %1\n").arg(page):QString())+row["evidence"].toString());
    const QFileInfo file(row["pdf"].toString());
    form.openEvidence->setEnabled(file.isFile() && file.suffix().compare("pdf",Qt::CaseInsensitive)==0);
  };
  connect(grid,&QTableWidget::currentCellChanged,dialog,refreshEvidence);
  auto edits=std::make_shared<QSettings>(reviewOverridesPath(),QSettings::IniFormat);
  connect(grid,&QTableWidget::itemChanged,dialog,[form,grid,edits](QTableWidgetItem *item){
    if(item->column()==Result)return;
    form.reviewOverlap->setChecked(false);
    const auto id=grid->item(item->row(),Id);
    if(!id)return;
    const auto row=id->data(Qt::UserRole).toJsonObject();
    if(row.contains("pdf") && item->column()<=Unit) {
      const QSignalBlocker block(grid);
      const bool missing=item->text().trimmed().isEmpty();
      item->setBackground(missing?QBrush(QColor("#fff0d2")):QBrush());
      item->setToolTip(missing?"Review this missing field":QString());
    }
    const auto key=row["override_key"].toString();
    if(!key.isEmpty() && grid->item(item->row(),Result)) {
      QJsonArray cells;
      for(int column=0;column<Result;++column)cells.append(grid->item(item->row(),column)->text());
      edits->setValue(key,QJsonDocument(cells).toJson(QJsonDocument::Compact));
      edits->setValue(stableRowKey(row),QJsonDocument(QJsonObject{{"original",row["import_cells"]},{"cells",cells}}).toJson(QJsonDocument::Compact));
    }
  });
  refreshEvidence();
  auto filters=new QHBoxLayout;
  auto groupFilter=new QComboBox(dialog);groupFilter->setObjectName("reviewSubsystem");groupFilter->addItem("All subsystems",QString());
  QStringList groups;
  for(int row=0;row<grid->rowCount();++row)groups.append(rowGroup(grid->item(row,Id)->data(Qt::UserRole).toJsonObject()));
  groups.removeDuplicates();for(const auto &group:groups)groupFilter->addItem(group,group);
  groupFilter->setCurrentIndex(std::max(0,groupFilter->findData(workspaceSubsystem)));
  auto unresolved=new QCheckBox("Unresolved only",dialog);unresolved->setObjectName("unresolvedOnly");
  auto advanced=new QToolButton(dialog);advanced->setText("Bit fields and calibration");advanced->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);advanced->setCheckable(true);advanced->setArrowType(Qt::RightArrow);advanced->setObjectName("advancedMapping");
  auto acceptChanged=new QPushButton("Accept selected changes",dialog);acceptChanged->setToolTip("Check the PDF and selected raw words before accepting a changed source definition.");
  filters->addWidget(groupFilter);filters->addWidget(unresolved);filters->addStretch();filters->addWidget(acceptChanged);filters->addWidget(advanced);form.layout->insertLayout(1,filters);
  const QList<int> detailColumns{Rt,Sa,Direction,Count,Lsb,Signed,Order,Scale,Offset,Channel,Bus,Reference};
  for(const auto column:detailColumns)grid->setColumnHidden(column,true);
  grid->horizontalHeader()->setStretchLastSection(true);
  connect(advanced,&QToolButton::toggled,dialog,[grid,advanced,detailColumns](bool show){advanced->setArrowType(show?Qt::DownArrow:Qt::RightArrow);for(const auto column:detailColumns)grid->setColumnHidden(column,!show);});
  const auto applyFilter=[grid,groupFilter,unresolved,form] {
    const QSignalBlocker block(grid);
    for(int row=0;row<grid->rowCount();++row){
      const auto data=grid->item(row,Id)->data(Qt::UserRole).toJsonObject();
      aero::Signal signal;auto error=mappingAt(grid,row,signal);
      if(data["definition_changed"].toBool())error="Source definition changed. Review and accept this row.";
      grid->item(row,Result)->setText(error.isEmpty()?"Ready":error);
      const bool out=!groupFilter->currentData().toString().isEmpty() && rowGroup(data)!=groupFilter->currentData().toString();
      grid->setRowHidden(row,out || (unresolved->isChecked() && error.isEmpty()));
    }
    form.publish->setText(groupFilter->currentData().toString().isEmpty()?"Publish profile":"Publish subsystem");
  };
  auto filterTimer=new QTimer(dialog);filterTimer->setSingleShot(true);filterTimer->setInterval(30);
  connect(filterTimer,&QTimer::timeout,dialog,applyFilter);
  connect(grid,&QTableWidget::itemChanged,dialog,[filterTimer](QTableWidgetItem *item){if(item->column()!=Result)filterTimer->start();});
  connect(groupFilter,&QComboBox::currentIndexChanged,dialog,applyFilter);connect(unresolved,&QCheckBox::toggled,dialog,applyFilter);
  connect(acceptChanged,&QPushButton::clicked,dialog,[grid,applyFilter]{QSet<int> rows;for(auto item:grid->selectedItems())if(!grid->isRowHidden(item->row()))rows.insert(item->row());if(rows.empty() && grid->currentRow()>=0)rows.insert(grid->currentRow());for(const auto row:rows){auto item=grid->item(row,Id);auto data=item->data(Qt::UserRole).toJsonObject();data["definition_changed"]=false;item->setData(Qt::UserRole,data);}applyFilter();});
  applyFilter();
  const auto tableGroups=[grid] {
    QStringList groups;
    for(int row=0;row<grid->rowCount();++row) {
      const auto data=grid->item(row,Id)->data(Qt::UserRole).toJsonObject();
      if(data["table_icd"].toBool())groups.append(data["subsystem"].toString());
    }
    groups.removeDuplicates();
    return groups;
  };
  form.tableSettings->setVisible(true);form.tableSettings->setText("Apply conversion preset…");
  connect(form.tableSettings,&QPushButton::clicked,dialog,[grid,dialog,form,groups,groupFilter,edits] {
    QDialog settingsDialog(dialog);settingsDialog.setWindowTitle("Preview conversion preset");settingsDialog.setObjectName("conversionPresetDialog");
    auto layout=new QFormLayout(&settingsDialog);
    auto help=new QLabel("Choose shared settings, inspect the affected cells, then apply. Existing values are preserved by default.",&settingsDialog);help->setWordWrap(true);layout->addRow(help);
    auto name=new QLineEdit("Reviewed conversion",&settingsDialog);name->setObjectName("presetName");layout->addRow("Preset name",name);
    auto selectedOnly=new QCheckBox("Selected rows only",&settingsDialog);layout->addRow(selectedOnly);
    auto replaceValues=new QCheckBox("Replace existing values (including manual edits)",&settingsDialog);replaceValues->setObjectName("presetReplace");layout->addRow(replaceValues);
    auto order=new QComboBox(&settingsDialog);order->setObjectName("tableFloatOrder");order->addItems({"Leave for review","MSW first","LSW first"});layout->addRow("Multi-word order",order);
    auto direct=new QCheckBox("I confirm ×1 + 0 where scaling is unspecified",&settingsDialog);direct->setObjectName("tableIdentityScale");layout->addRow(direct);
    auto scale=new QLineEdit(&settingsDialog),offset=new QLineEdit(&settingsDialog);scale->setObjectName("presetScale");offset->setObjectName("presetOffset");layout->addRow("Confirmed scale",scale);layout->addRow("Confirmed offset",offset);
    QHash<QString,QComboBox *> channels;
    for(const auto &group:groups){auto channel=new QComboBox(&settingsDialog);channel->setObjectName("tableChannel");channel->setProperty("subsystem",group);channel->addItems({"Leave for review","CH1","CH2"});layout->addRow(group+" channel",channel);channels.insert(group,channel);}
    auto preview=new QTableWidget(0,4,&settingsDialog);preview->setObjectName("presetPreview");preview->setHorizontalHeaderLabels({"Signal","Field","Before","After"});preview->setEditTriggers(QAbstractItemView::NoEditTriggers);preview->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);layout->addRow(preview);
    auto summary=new QLabel(&settingsDialog);layout->addRow(summary);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel,&settingsDialog);layout->addRow(buttons);
    const auto changes=[=] {
      QList<QPair<QTableWidgetItem *,QString>> result;QSet<int> selected;
      for(auto item:grid->selectedItems())selected.insert(item->row());
      for(int row=0;row<grid->rowCount();++row){
        const auto data=grid->item(row,Id)->data(Qt::UserRole).toJsonObject();const auto group=rowGroup(data);
        if((!groupFilter->currentData().toString().isEmpty() && group!=groupFilter->currentData().toString()) || (selectedOnly->isChecked() && !selected.contains(row)))continue;
        const auto add=[&](int column,QString value){auto item=grid->item(row,column);if(!value.isEmpty() && value!=item->text() && (replaceValues->isChecked() || item->text().trimmed().isEmpty()))result.append({item,value});};
        if(order->currentIndex()>0 && grid->item(row,Width)->text().toInt()+grid->item(row,Lsb)->text().toInt()>16)add(Order,order->currentIndex()==1?"msw-first":"lsw-first");
        add(Scale,scale->text().trimmed().isEmpty()?(direct->isChecked()?"1":""):scale->text().trimmed());
        add(Offset,offset->text().trimmed().isEmpty()?(direct->isChecked()?"0":""):offset->text().trimmed());
        if(channels.contains(group) && channels[group]->currentIndex()>0)add(Channel,QString::number(channels[group]->currentIndex()));
      }
      return result;
    };
    const auto update=[=]{const auto affected=changes();preview->setRowCount(affected.size());int row=0;
      for(const auto &change:affected){const auto item=change.first;const QStringList cells{grid->item(item->row(),Id)->text(),headers[item->column()],item->text().isEmpty()?"Missing":item->text(),change.second};for(int column=0;column<4;++column)preview->setItem(row,column,new QTableWidgetItem(cells[column]));++row;}
      bool valid=true;for(const auto input:{scale,offset})if(!input->text().trimmed().isEmpty()){bool ok=false;const auto value=input->text().toDouble(&ok);valid&=ok && std::isfinite(value);}
      summary->setText(valid?QString("%1 cells will change. No changes are published until signal review is complete.").arg(affected.size()):"Enter finite numeric scale and offset.");buttons->button(QDialogButtonBox::Apply)->setEnabled(valid && !affected.empty());
    };
    for(auto input:{scale,offset})connect(input,&QLineEdit::textChanged,&settingsDialog,update);
    for(auto input:{direct,selectedOnly,replaceValues})connect(input,&QCheckBox::toggled,&settingsDialog,update);
    connect(order,&QComboBox::currentIndexChanged,&settingsDialog,update);for(auto channel:channels)connect(channel,&QComboBox::currentIndexChanged,&settingsDialog,update);
    connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,&settingsDialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&settingsDialog,&QDialog::reject);update();settingsDialog.resize(800,650);
    if(settingsDialog.exec()!=QDialog::Accepted)return;
    const auto affected=changes();QHash<QString,QJsonObject> presets;
    for(const auto &change:affected){const auto data=grid->item(change.first->row(),Id)->data(Qt::UserRole).toJsonObject();auto preset=presets.value(presetKey(data));preset[headers[change.first->column()]]=change.second;preset["name"]=name->text().trimmed();
      QJsonArray cells;for(int column=0;column<Result;++column)cells.append(grid->item(change.first->row(),column)->text());
      auto layouts=preset["compatible_layouts"].toArray();const auto signature=calibrationLayout(cells);if(!layouts.contains(signature))layouts.append(signature);preset["compatible_layouts"]=layouts;
      presets[presetKey(data)]=preset;change.first->setText(change.second);}
    for(auto it=presets.begin();it!=presets.end();++it){auto saved=QJsonDocument::fromJson(edits->value(it.key()).toByteArray()).object();for(auto field=it.value().begin();field!=it.value().end();++field){
      if(field.key()=="compatible_layouts"){auto layouts=saved[field.key()].toArray();for(const auto &entry:field.value().toArray())if(!layouts.contains(entry))layouts.append(entry);saved[field.key()]=layouts;}else saved[field.key()]=field.value();}edits->setValue(it.key(),QJsonDocument(saved).toJson(QJsonDocument::Compact));}
    form.reviewStatus->setText(QString("Applied preset to %1 cells. Preview a known raw value before publication.").arg(affected.size()));
  });
  grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
  auto applyShared=new QPushButton("Fill selected cells…",dialog);
  dialog->layout()->addWidget(applyShared);
  connect(applyShared,&QPushButton::clicked,dialog,[grid,dialog,form]{
    const auto selected=grid->selectedItems();
    if(selected.isEmpty()){form.reviewStatus->setText("Select cells in one field column.");return;}
    const auto column=selected.front()->column();
    if(column>=Result || std::any_of(selected.begin(),selected.end(),[column](const auto *cell){return cell->column()!=column;})) {
      form.reviewStatus->setText("Select cells in one field column.");return;
    }
    bool accepted=false;
    const auto value=QInputDialog::getText(dialog,"Fill selected cells",
      QString("Value for %1").arg(headers[column]),QLineEdit::Normal,selected.front()->text(),&accepted);
    if(accepted) for(auto *cell:selected) cell->setText(value);
  });
  connect(form.openEvidence,&QPushButton::clicked,dialog,[selected]{
    const auto row=selected();
    const QFileInfo file(row["pdf"].toString());
    if(!file.isFile() || file.suffix().compare("pdf",Qt::CaseInsensitive)!=0)return;
    auto url=QUrl::fromLocalFile(file.absoluteFilePath());
    if(row["page"].toInt()>0)url.setFragment("page="+QString::number(row["page"].toInt()));
    QDesktopServices::openUrl(url);
  });
  const QString demoHash="f5dcf391febfbf4eeee51e5a9e8b5cbac1f4b66681081e49f71a1c08a6769fb7";
  const auto isDemo=[demoHash](const QJsonObject &row){return row["mapping"].toObject()["source_hash"].toString()==demoHash && !row["draft_field"].toObject().isEmpty();};
  bool demo=false;
  for(int row=0;row<grid->rowCount();++row)demo|=isDemo(grid->item(row,Id)->data(Qt::UserRole).toJsonObject());
  form.demoAssumptions->setText("Fill example mappings");
  form.demoAssumptions->setVisible(demo);form.demoBits->setVisible(demo);form.demoBits->setEnabled(false);
  connect(form.demoAssumptions,&QPushButton::clicked,dialog,[grid,form,insert,isDemo]{
    const auto profile=aero::exampleSimulationProfile();
    for(int row=0;row<grid->rowCount();++row) {
      auto data=grid->item(row,Id)->data(Qt::UserRole).toJsonObject();
      if(!isDemo(data))continue;
      const auto word=unsigned(data["draft_field"].toObject()["word_index"].toInt(-1));
      const auto rule=std::find_if(profile.begin(),profile.begin()+18,[word](const auto &s){return s.word==word;});
      if(rule==profile.begin()+18)continue;
      auto signal=*rule;
      const auto provenance=data["mapping"].toObject();
      signal.sourceDocument=provenance["source_document"].toString().toStdString();
      signal.sourceHash=provenance["source_hash"].toString().toStdString();signal.sourcePage=data["page"].toInt();
      const auto filled=reviewRow(desktop::signalToJson(signal));
      data["cells"]=filled["cells"];data["mapping"]=filled["mapping"];
      insert(row,data);
    }
    form.demoBits->setEnabled(true);
    form.reviewStatus->setText("Demo assumptions filled. Publish and select Offline simulation to decode these signals.");
  });
  connect(form.demoBits,&QPushButton::clicked,dialog,[form,append,grid,isDemo]{
    QJsonArray rows;
    QJsonObject evidence;
    for(int row=0;row<grid->rowCount();++row) {
      const auto data=grid->item(row,Id)->data(Qt::UserRole).toJsonObject();
      if(isDemo(data)){evidence=data;break;}
    }
    const auto profile=aero::exampleSimulationProfile();
    for(auto it=profile.begin()+18;it!=profile.end();++it){
      auto signal=*it;
      signal.sourceDocument=evidence["mapping"].toObject()["source_document"].toString().toStdString();
      signal.sourceHash=evidence["mapping"].toObject()["source_hash"].toString().toStdString();
      signal.sourcePage=evidence["page"].toInt();
      auto row=reviewRow(desktop::signalToJson(signal));row["pdf"]=evidence["pdf"];row["page"]=evidence["page"];
      row["evidence"]="Synthetic bit mapping from words 28–30. MIL-STD-1553 bit 4 is the data MSB.";
      rows.append(row);
    }
    if(append(rows))form.demoBits->setEnabled(false);
  });
  const auto preview=[this,form,grid](bool hex) {
    aero::Signal signal;
    if(auto error=mappingAt(grid,grid->currentRow(),signal);!error.isEmpty()){form.reviewStatus->setText(error);return;}
    auto record=backend.selected;
    if(hex) {
      const auto pieces=form.hexWords->text().split(QRegularExpression("[ ,]+"),Qt::SkipEmptyParts);
      const auto count=(signal.lsb+signal.width+15)/16;
      if(pieces.size()!=count){form.reviewStatus->setText(QString("Enter exactly %1 field words in hex.").arg(count));return;}
      record={};record.id=1;record.channel=signal.channel?signal.channel:1;record.bus=signal.bus<0?0:signal.bus;
      record.words.resize(signal.expectedWordCount?signal.expectedWordCount:signal.word+count);
      for(unsigned i=0;i<count;++i){bool ok;const auto word=pieces[i].toUInt(&ok,16);if(!ok || word>65535){form.reviewStatus->setText("Invalid 16-bit hexadecimal word.");return;}record.words[signal.word+i]=std::uint16_t(word);}
      record.commandWord=(signal.rt<<11)|(signal.transmit<<10)|(signal.sa<<5)|(record.words.size()&31);
      record.statusWord=signal.rt<<11;
    }
    if(!record.id){form.reviewStatus->setText("Select a captured or replayed record first.");return;}
    const auto protocol=aero::interpret(record);
    const auto route=protocol.payloadCommand;
    const auto routeText=QString("RT %1 / SA %2 / %3 / %4 words · CH%5/%6")
      .arg(route.rt).arg(route.sa).arg(route.transmit?"Tx":"Rx").arg(route.count)
      .arg(record.channel).arg(record.bus?"B":"A");
    if(!aero::matchesSignalRoute(record,signal,route)){
      form.reviewStatus->setText(routeText+" · Route or stream does not match this mapping.");return;
    }
    const auto context=hex || backend.exampleContext();
    auto value=aero::decodeValue(record,signal,protocol,context);
    QStringList raw;for(unsigned i=0;i<value.rawCount;++i)raw.append(QString("%1").arg(value.raw[i],4,16,QLatin1Char('0')));
    if(!value.value){form.reviewStatus->setText(routeText+" · Raw: "+raw.join(' ')+" · "+QString::fromUtf8(value.reason.data(),qsizetype(value.reason.size())));return;}
    auto unscaled=signal;unscaled.scale=1;unscaled.offset=0;
    const auto decoded=aero::decode(record,unscaled,context);
    const auto representation=signal.numericType=="integer"?QString(signal.signedValue?"signed integer":"unsigned integer"):QString::fromStdString(signal.numericType);
    form.reviewStatus->setText(routeText+" · "+representation+" · "+
      (signal.leastSignificantWordFirst?"LSW first":"MSW first")+" · "+QString("Raw %1 · decoded %2 · × %3 + %4 = %5 %6 · %7")
      .arg(raw.join(' '),decoded?QString::number(*decoded,'g',15):QString("—"),QString::number(signal.scale,'g',15),
           QString::number(signal.offset,'g',15),QString::number(*value.value,'g',15),QString::fromStdString(signal.unit),
           hex?"Synthetic preview":"Valid sample"));
  };
  connect(form.previewHex,&QPushButton::clicked,dialog,[preview]{preview(true);});
  form.previewRecord->setText("Verify against selected message");
  connect(form.previewRecord,&QPushButton::clicked,dialog,[preview]{preview(false);});
  connect(form.saveDraft,&QPushButton::clicked,dialog,[this,dialog,form,grid]{
    QJsonArray rows;
    for(int row=0;row<grid->rowCount();++row) {
      auto data=grid->item(row,Id)->data(Qt::UserRole).toJsonObject();
      QJsonArray cells;for(int col=0;col<Result;++col)cells.append(grid->item(row,col)->text());
      data["cells"]=cells;rows.append(data);
    }
    const auto path=QFileDialog::getSaveFileName(dialog,"Save signal draft",sessionDir+"/signal-draft.json","JSON (*.json)");
    if(path.isEmpty())return;
    QSaveFile file(path);
    const auto data=QJsonDocument(QJsonObject{{"schema","aerotrace.icd.review.v1"},{"draft",true},{"rows",rows}}).toJson();
    if(data.size()>4*1024*1024){form.reviewStatus->setText("Draft exceeds 4 MB.");return;}
    form.reviewStatus->setText(file.open(QIODevice::WriteOnly) && file.write(data)==data.size() && file.commit()?"Draft saved. Active profile is unchanged.":"Could not save the draft.");
  });
  connect(form.publish,&QPushButton::clicked,dialog,[this,dialog,form,grid,groupFilter]{
    const auto selectedGroup=groupFilter->currentData().toString();
    std::vector<aero::Signal> rules;
    if(!selectedGroup.isEmpty())for(const auto &signal:backend.profile)if((signal.subsystem.empty()?QString("Unassigned"):QString::fromStdString(signal.subsystem))!=selectedGroup)rules.push_back(signal);
    std::vector<bool> tableRules(rules.size(),false);
    QHash<QString,int> ids;
    int firstError=-1;
    for(int row=0;row<grid->rowCount();++row) {
      const auto rowData=grid->item(row,Id)->data(Qt::UserRole).toJsonObject();
      if(!selectedGroup.isEmpty() && rowGroup(rowData)!=selectedGroup)continue;
      aero::Signal signal;
      auto error=mappingAt(grid,row,signal);
      if(rowData["definition_changed"].toBool())error="Source definition changed. Review and accept this row.";
      const bool table=grid->item(row,Id)->data(Qt::UserRole).toJsonObject()["table_icd"].toBool();
      if(error.isEmpty()) {
        const auto id=QString::fromStdString(signal.id);
        if(ids.contains(id))error="Duplicate signal ID. Edit this row or remove it.";
        else {
          ids.insert(id,row);
          for(std::size_t i=0;i<rules.size() && error.isEmpty();++i) {
            if(!overlaps(signal,rules[i]))continue;
            const auto other=QString::fromStdString(rules[i].id);
            if((table || tableRules[i]) && id.contains(" / ") && other.contains(" / ") &&
                id.section(" / ",0,-2)!=other.section(" / ",0,-2))
              error="Different ICD groups share this field and route. Assign separate channels or remove one group.";
            else if(!form.reviewOverlap->isChecked())
              error="Overlapping field. Verify both mappings, then confirm overlaps.";
          }
        }
      }
      grid->item(row,Result)->setText(error.isEmpty()?"Ready":error);
      if(!error.isEmpty() && firstError<0)firstError=row;
      if(error.isEmpty()){rules.push_back(std::move(signal));tableRules.push_back(table);}
    }
    if(firstError>=0){grid->showRow(firstError);grid->setCurrentCell(firstError,Id);grid->setFocus();form.reviewStatus->setText(QString("Row %1 · %2").arg(firstError+1).arg(grid->item(firstError,Result)->text()));return;}
    if(rules.empty()){form.reviewStatus->setText("Add at least one signal before publishing.");return;}
    if(auto error=publishProfile(rules);!error.isEmpty()){form.reviewStatus->setText(error);return;}
    workspaceSubsystem=selectedGroup;workspaceScopeKey="live";settings.setValue("workspace/live/subsystem",workspaceSubsystem);
    const auto selected=std::find_if(backend.profile.begin(),backend.profile.end(),[this](const auto &signal){return inWorkspace(signal);});
    if(selected!=backend.profile.end()){preferredSignalIndex=unsigned(selected-backend.profile.begin());if(selected->channel)analysisChannel=selected->channel;}
    dialog->accept();
    if(documentDock)documentDock->hide();
    if(documentDialog)documentDialog->hide();
    navigate(6);
    toast(QString("Published %1 signals. Select a signal to plot.").arg(rules.size()));
  });
  connect(form.closeReview,&QPushButton::clicked,dialog,&QDialog::reject);
  dialog->resize(qMin(1240,screen()->availableGeometry().width()-40),qMin(680,screen()->availableGeometry().height()-60));
  dialog->show();grid->setFocus();
}
