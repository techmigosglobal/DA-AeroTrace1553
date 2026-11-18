#pragma once
#include <QtCore>
#include "record_csv.hpp"
#include <atomic>
#include <functional>

namespace desktop {
inline const QRegularExpression &csvWhitespace() {
  static const QRegularExpression expression(QStringLiteral("\\s+"));
  return expression;
}
inline QStringList csvCells(const QByteArray &bytes) {
  auto text=QString::fromUtf8(bytes);if(text.startsWith(QChar(0xfeff)))text.remove(0,1);
  if(!text.contains('"')){if(text.endsWith('\n'))text.chop(1);if(text.endsWith('\r'))text.chop(1);return text.split(',');}
  QStringList cells;QString cell;bool quoted=false;
  for(int i=0;i<text.size();++i){const auto c=text[i];if(c=='"'){if(quoted&&i+1<text.size()&&text[i+1]=='"'){cell+='"';++i;}else quoted=!quoted;}else if(c==','&&!quoted){cells<<cell;cell.clear();}else if((c!='\r'&&c!='\n')||quoted)cell+=c;}
  if(quoted)throw std::runtime_error("Unterminated CSV quote");cells<<cell;return cells;
}
inline QByteArray csvLine(const QStringList &cells) {
  QStringList escaped;for(auto value:cells){if(value.contains(',')||value.contains('"')||value.contains('\n')||value.contains('\r')){value.replace("\"","\"\"");value='"'+value+'"';}escaped<<value;}return escaped.join(',').toUtf8()+'\n';
}
inline QByteArray csvRow(QFile &file) {
  QByteArray result;bool quoted=false;
  do{const auto part=file.readLine(4097);if(part.isEmpty()){if(!result.isEmpty())throw std::runtime_error("Incomplete CSV row");return {};}result+=part;if(result.size()>1024*1024)throw std::runtime_error("CSV row exceeds 1 MiB");for(char ch:part)if(ch=='"')quoted=!quoted;if(!quoted&&(part.endsWith('\n')||file.atEnd()))break;}while(!file.atEnd());
  if(quoted)throw std::runtime_error("Unterminated CSV quote");
  return result;
}
inline std::uint16_t csvWord(QStringView value,int radix) {
  value=value.trimmed();if(value.size()>=2&&value[0]=='0'){const auto prefix=value[1].toLower();if(prefix=='x'||prefix=='b'||prefix=='o'){radix=prefix=='x'?16:prefix=='b'?2:8;value=value.sliced(2);}}
  if(!value.isEmpty()&&value[0]=='+')value=value.sliced(1);
  if(value.isEmpty()||(radix!=2&&radix!=8&&radix!=10&&radix!=16))throw std::runtime_error("Invalid unsigned 16-bit word");
  unsigned word=0;for(const auto character:value){const auto c=character.unicode();const unsigned digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:16;if(digit>=unsigned(radix)||word>(65535-digit)/unsigned(radix))throw std::runtime_error("Invalid unsigned 16-bit word");word=word*radix+digit;}return std::uint16_t(word);
}
inline std::uint16_t csvWord(const QString &value,int radix){return csvWord(QStringView(value),radix);}
template<class Visitor> void csvWordTokens(QStringView text,Visitor visit) {
  unsigned count=0;qsizetype start=0;while(start<text.size()){while(start<text.size()&&text[start].isSpace())++start;if(start==text.size())break;auto end=start;while(end<text.size()&&!text[end].isSpace())++end;if(++count>32)throw std::runtime_error("More than 32 payload words");visit(text.sliced(start,end-start));start=end;}
}
inline std::uint64_t csvTime(QString text,bool compact) {
  static const QRegularExpression compactTime(QStringLiteral("^(\\d+):(\\d{1,2}):(\\d{1,2}):(\\d{1,2}):(\\d{1,6})$"));
  static const QRegularExpression legacyTime(QStringLiteral("^(\\d+):(\\d{1,2}):(\\d{1,2})[ .](\\d{3})\\.(\\d{3})$"));
  auto match=(compact?compactTime:legacyTime).match(text.trimmed());
  if(!match.hasMatch())throw std::runtime_error("Time does not match the selected CSV layout");
  bool timeOk=false;const auto a=match.captured(1).toULongLong(&timeOk);if(!timeOk)throw std::runtime_error("Timestamp component exceeds range");const auto b=match.captured(2).toUInt(),c=match.captured(3).toUInt(),d=match.captured(4).toUInt(),e=match.captured(5).toUInt();
  if((compact&&(b>23||c>59||d>59))||(!compact&&(b>59||c>59)))throw std::runtime_error("Invalid time components");
  const long double us=compact?((static_cast<long double>(a)*24+b)*3600+c*60+d)*1000000+e:(static_cast<long double>(a)*3600+b*60+c)*1000000+d*1000+e;
  if(us>static_cast<long double>(UINT64_MAX)/1000)throw std::runtime_error("Timestamp exceeds native recording range");return std::uint64_t(us)*1000;
}
struct CsvInput {
  QStringList header;
  bool compact{};
  explicit CsvInput(QFile &file){header=csvCells(csvRow(file));compact=header==QString::fromStdString(aero::sessionCsvHeader()).trimmed().split(',');if(!compact&&header!=QString::fromStdString(aero::legacySessionCsvHeader()).trimmed().split(','))throw std::runtime_error("Expected the compact 11-column or legacy 42-column session CSV");}
  void validate(const QStringList &row)const{
    if(row.size()==header.size())return;
    // Historical supplier exports have 42 named fields followed by empty padding.
    // Admit only empty trailing cells; never ignore additional payload or text.
    if(!compact&&row.size()>header.size()&&std::all_of(row.begin()+header.size(),row.end(),[](const QString &cell){return cell.trimmed().isEmpty();}))return;
    throw std::runtime_error("CSV field count differs from its header");
  }
  QStringList convert(QStringList row,int inputBase,aero::NumericRadix outputBase)const {
    validate(row);row=row.mid(0,header.size());for(int column:compact?QList<int>{2,7,9}:QList<int>{2,7,8})if(!row[column].trimmed().isEmpty())row[column]=QString::fromStdString(aero::rawNumber(csvWord(row[column],inputBase),outputBase));
    for(int column:{5,6}){auto text=row[column].trimmed();if(text.isEmpty())continue;auto token=text.section(csvWhitespace(),0,0);const auto word=csvWord(token,inputBase);const auto encoded=outputBase==aero::NumericRadix::Hexadecimal?aero::csvHex(word):aero::rawNumber(word,outputBase);row[column]=QString::fromStdString(encoded)+text.mid(token.size());}
    if(compact){std::string data;data.reserve(640);csvWordTokens(QStringView(row[8]),[&](QStringView token){if(!data.empty())data+="  ";data+=aero::rawNumber(csvWord(token,inputBase),outputBase,true);});row[8]=QString::fromStdString(data);}
    else for(int column=10;column<42;++column)if(!row[column].trimmed().isEmpty()&&row[column].trimmed()!="-")row[column]=QString::fromStdString(aero::rawNumber(csvWord(row[column],inputBase),outputBase));
    return row;
  }
  aero::Record record(const QStringList &row,int inputBase,unsigned channel,quint64 index)const {
    validate(row);aero::Record r;r.id=index;r.timestampNs=csvTime(row[0],compact);r.channel=channel;
    if(row[4].trimmed()!="A"&&row[4].trimmed()!="B")throw std::runtime_error("Bus must be A or B");r.bus=row[4].trimmed()=="B";
    const auto command=[&](int col){return csvWord(row[col].trimmed().section(csvWhitespace(),0,0),inputBase);};
    if(!row[5].trimmed().isEmpty()){r.commandWord=command(5);r.validity|=1;}
    if(!row[6].trimmed().isEmpty()){if(!row[5].trimmed().isEmpty()){r.commandWord2=command(6);r.validity|=2;}else{r.commandWord=command(6);r.validity|=1;}}
    const auto tx=!row[7].trimmed().isEmpty(),rx=!row[compact?9:8].trimmed().isEmpty();
    if(r.validity&2){if(tx){r.statusWord=csvWord(row[7],inputBase);r.validity|=4;}if(rx){r.statusWord2=csvWord(row[compact?9:8],inputBase);r.validity|=8;}}
    else if(aero::command(r.commandWord).transmit){if(tx){r.statusWord=csvWord(row[7],inputBase);r.validity|=4;}}else if(rx){r.statusWord=csvWord(row[compact?9:8],inputBase);r.validity|=4;}
    if(compact){csvWordTokens(QStringView(row[8]),[&](QStringView token){r.words.push_back(csvWord(token,inputBase));});if(!row[10].trimmed().isEmpty())r.flags=1;}
    else {bool missing=false;for(int i=10;i<42;++i){if(row[i].trimmed().isEmpty()||row[i].trimmed()=="-"){missing=true;continue;}if(missing)throw std::runtime_error("Non-contiguous payload words");r.words.push_back(csvWord(row[i],inputBase));}if(row[9].toUInt()!=r.words.size())throw std::runtime_error("WordCount differs from supplied payload words");}
    if(r.words.size()>32)throw std::runtime_error("More than 32 payload words");
    // These layouts store normalized DDC monitor block status, but do not prove
    // hardware provenance. Keep source=0; retain explicit CSV presence and errors.
    if(!row[2].trimmed().isEmpty()){r.vendorStatus=csvWord(row[2],inputBase);r.flags|=r.vendorStatus&0x163f;}
    const auto first=aero::command(r.commandWord),second=aero::command(r.commandWord2);
    if(!(r.validity&1)||(!first.broadcast&&!(r.validity&4))||((r.validity&2)&&!(r.validity&4))||((r.validity&2)&&!first.broadcast&&!(r.validity&8)))r.flags|=1;
    if((r.validity&4)&&(r.statusWord>>11)!=((r.validity&2)?second.rt:first.rt))r.flags|=1;
    if((r.validity&8)&&(r.statusWord2>>11)!=first.rt)r.flags|=1;
    return r;
  }
};
inline aero::CsvExportOptions csvOptions(const QSettings &settings) {
  aero::CsvExportOptions result;result.layout=settings.value("csvLayout","compact11").toString()=="legacy42"?aero::CsvLayout::Legacy42:aero::CsvLayout::Compact11;
  const auto radix=settings.value("csvRadix",16).toUInt();if(radix==2||radix==8||radix==10||radix==16)result.radix=static_cast<aero::NumericRadix>(radix);return result;
}
}
