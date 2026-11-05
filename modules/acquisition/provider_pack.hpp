#pragma once
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace aero {
struct DdcQualificationReceipt {
  unsigned windowsMinBuild{},windowsMaxBuild{};
  std::string driverVersion,hardwareId,receipt;
};
struct DdcProviderPack {
  std::filesystem::path manifest,providerPath,runtimePath;
  std::string id,sdkIdentity,headerSha256,providerSha256,runtimeSha256,fileVersion,error;
  std::vector<DdcQualificationReceipt> matrix;
  bool receiptsComplete{};
};
namespace provider_pack_detail {
// A bounded JSON reader: duplicate keys, trailing input, malformed Unicode and
// excessive nesting/counts fail rather than granting partial compatibility.
struct Json {
  enum Kind { Null,Object,Array,String,Number,Boolean } kind{Null};
  std::map<std::string,Json> object;std::vector<Json> array;std::string text;bool boolean{};
  const Json &at(const std::string &key) const {auto i=object.find(key);if(kind!=Object||i==object.end())throw std::runtime_error("Missing pack field: "+key);return i->second;}
  std::string string() const {if(kind!=String)throw std::runtime_error("Pack field must be a string");return text;}
  bool flag() const {if(kind!=Boolean)throw std::runtime_error("Pack receipt must be boolean");return boolean;}
  unsigned integer() const {unsigned value{};auto r=std::from_chars(text.data(),text.data()+text.size(),value);if(kind!=Number||r.ec!=std::errc()||r.ptr!=text.data()+text.size())throw std::runtime_error("Pack field must be an unsigned integer");return value;}
};
class Parser {
  const std::string &input;std::size_t offset{},nodes{};
  void whitespace(){while(offset<input.size()&&(input[offset]==' '||input[offset]=='\r'||input[offset]=='\n'||input[offset]=='\t'))++offset;}
  char take(){if(offset>=input.size())throw std::runtime_error("Truncated pack JSON");return input[offset++];}
  bool consume(char c){whitespace();if(offset<input.size()&&input[offset]==c){++offset;return true;}return false;}
  void expect(char c){if(!consume(c))throw std::runtime_error("Invalid pack JSON separator");}
  unsigned hex(){unsigned v{};for(unsigned i=0;i<4;++i){auto c=take();v<<=4;if(c>='0'&&c<='9')v+=c-'0';else if(c>='a'&&c<='f')v+=c-'a'+10;else if(c>='A'&&c<='F')v+=c-'A'+10;else throw std::runtime_error("Invalid JSON Unicode escape");}return v;}
  static void unicode(std::string &out,unsigned cp){if(cp<=0x7f)out+=char(cp);else if(cp<=0x7ff){out+=char(0xc0|(cp>>6));out+=char(0x80|(cp&63));}else if(cp<=0xffff){out+=char(0xe0|(cp>>12));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}else{out+=char(0xf0|(cp>>18));out+=char(0x80|((cp>>12)&63));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}}
  std::string string(){
    expect('"');std::string out;
    while(true){auto c=take();if(c=='"')return out;if(static_cast<unsigned char>(c)<32)throw std::runtime_error("Control character in pack JSON");
      if(c=='\\'){switch(c=take()){
        case '"':case '\\':case '/':out+=c;break;case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;
        case 'u':{unsigned cp=hex();if(cp>=0xd800&&cp<=0xdbff){if(take()!='\\'||take()!='u')throw std::runtime_error("Unpaired JSON surrogate");auto low=hex();if(low<0xdc00||low>0xdfff)throw std::runtime_error("Unpaired JSON surrogate");cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);}else if(cp>=0xdc00&&cp<=0xdfff)throw std::runtime_error("Unpaired JSON surrogate");unicode(out,cp);break;}
        default:throw std::runtime_error("Invalid pack JSON escape");}
      }else if(static_cast<unsigned char>(c)>=0x80){
        const auto lead=static_cast<unsigned char>(c);unsigned remaining{},cp{},minimum{};
        if(lead>=0xc2&&lead<=0xdf){remaining=1;cp=lead&31;minimum=0x80;}
        else if(lead>=0xe0&&lead<=0xef){remaining=2;cp=lead&15;minimum=0x800;}
        else if(lead>=0xf0&&lead<=0xf4){remaining=3;cp=lead&7;minimum=0x10000;}
        else throw std::runtime_error("Invalid JSON UTF-8");
        out+=c;for(unsigned i=0;i<remaining;++i){auto next=static_cast<unsigned char>(take());if((next&0xc0)!=0x80)throw std::runtime_error("Invalid JSON UTF-8 continuation");cp=(cp<<6)|(next&63);out+=char(next);}
        if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))throw std::runtime_error("Invalid JSON Unicode scalar");
      }else out+=c;
      if(out.size()>8192)throw std::runtime_error("Pack JSON string exceeds 8192 bytes");
    }
  }
  Json value(unsigned depth){
    if(depth>16||++nodes>4096)throw std::runtime_error("Pack JSON complexity limit");whitespace();if(offset>=input.size())throw std::runtime_error("Missing pack JSON value");Json j;auto c=input[offset];
    if(c=='{'){j.kind=Json::Object;++offset;if(consume('}'))return j;do{auto key=string();expect(':');if(!j.object.emplace(key,value(depth+1)).second)throw std::runtime_error("Duplicate pack JSON key");}while(consume(','));expect('}');}
    else if(c=='['){j.kind=Json::Array;++offset;if(consume(']'))return j;do{j.array.push_back(value(depth+1));}while(consume(','));expect(']');}
    else if(c=='"'){j.kind=Json::String;j.text=string();}
    else if(input.compare(offset,4,"true")==0){j.kind=Json::Boolean;j.boolean=true;offset+=4;}
    else if(input.compare(offset,5,"false")==0){j.kind=Json::Boolean;offset+=5;}
    else if(input.compare(offset,4,"null")==0){offset+=4;}
    else {j.kind=Json::Number;auto begin=offset;if(c=='-')++offset;if(offset>=input.size()||input[offset]<'0'||input[offset]>'9')throw std::runtime_error("Invalid JSON number");if(input[offset]=='0')++offset;else while(offset<input.size()&&input[offset]>='0'&&input[offset]<='9')++offset;
      if(offset<input.size()&&input[offset]=='.'){++offset;auto digit=offset;while(offset<input.size()&&input[offset]>='0'&&input[offset]<='9')++offset;if(offset==digit)throw std::runtime_error("Invalid JSON fraction");}
      if(offset<input.size()&&(input[offset]=='e'||input[offset]=='E')){++offset;if(offset<input.size()&&(input[offset]=='+'||input[offset]=='-'))++offset;auto digit=offset;while(offset<input.size()&&input[offset]>='0'&&input[offset]<='9')++offset;if(offset==digit)throw std::runtime_error("Invalid JSON exponent");}j.text=input.substr(begin,offset-begin);}
    return j;
  }
public:
  explicit Parser(const std::string &s):input(s){if(input.compare(0,3,"\xef\xbb\xbf")==0)offset=3;}
  Json parse(){auto j=value(0);whitespace();if(offset!=input.size())throw std::runtime_error("Trailing pack JSON input");return j;}
};
inline std::string hash(const Json &value){auto s=value.string();if(s.size()!=64||!std::all_of(s.begin(),s.end(),[](unsigned char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}))throw std::runtime_error("Pack SHA-256 must have 64 hex digits");std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(c>='A'&&c<='F'?c-'A'+'a':c);});return s;}
inline std::filesystem::path confined(const std::filesystem::path &root,const std::string &relative){
  if(relative.empty()||relative.find('\0')!=std::string::npos||relative.find(':')!=std::string::npos||relative.find('\\')!=std::string::npos)throw std::runtime_error("Pack paths must use relative slash-separated names");
  auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(relative.data()),relative.size()));if(path.is_absolute()||path.has_root_name())throw std::runtime_error("Absolute pack path rejected");for(const auto &part:path)if(part==".."||part==".")throw std::runtime_error("Pack path traversal rejected");
  std::error_code ec;auto base=std::filesystem::weakly_canonical(root,ec);if(ec)throw std::runtime_error("Pack root cannot be resolved");auto target=std::filesystem::weakly_canonical(base/path,ec);if(ec)throw std::runtime_error("Pack path cannot be resolved");
  auto b=base.begin(),t=target.begin();for(;b!=base.end();++b,++t)if(t==target.end()||*b!=*t)throw std::runtime_error("Pack symlink escapes its directory");if(t==target.end())throw std::runtime_error("Pack file path is its root");return target;
}
}
inline DdcProviderPack readDdcProviderPack(const std::filesystem::path &manifest){
  DdcProviderPack p;p.manifest=manifest;
  try{
    std::error_code ec;auto bytes=std::filesystem::file_size(manifest,ec);if(ec||bytes>1024*1024)throw std::runtime_error("Provider pack missing or exceeds 1 MiB");
    std::ifstream input(manifest,std::ios::binary);std::string data((std::istreambuf_iterator<char>(input)),{});if(!input.eof()&&input.fail())throw std::runtime_error("Provider pack read failed");if(data.size()!=bytes)throw std::runtime_error("Provider pack changed during read");
    const auto j=provider_pack_detail::Parser(data).parse();if(j.at("schema").string()!="aerotrace.provider-pack.v1")throw std::runtime_error("Unsupported provider pack schema");if(j.at("architecture").string()!="x64")throw std::runtime_error("Provider pack architecture must be x64");
    p.id=j.at("id").string();if(p.id.empty()||p.id.size()>128||!std::all_of(p.id.begin(),p.id.end(),[](unsigned char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-';}))throw std::runtime_error("Invalid provider pack id");
    const auto &provider=j.at("provider"),&runtime=j.at("runtime"),&sdk=j.at("sdk");if(provider.at("abi").integer()!=1)throw std::runtime_error("Unsupported provider pack ABI");
    p.providerPath=provider_pack_detail::confined(manifest.parent_path(),provider.at("path").string());p.runtimePath=provider_pack_detail::confined(manifest.parent_path(),runtime.at("path").string());
    p.providerSha256=provider_pack_detail::hash(provider.at("sha256"));p.runtimeSha256=provider_pack_detail::hash(runtime.at("sha256"));p.headerSha256=provider_pack_detail::hash(sdk.at("header_sha256"));p.sdkIdentity=sdk.at("identity").string();p.fileVersion=runtime.at("file_version").string();
    const auto &r=j.at("receipts");p.receiptsComplete=r.at("compiled").flag()&&r.at("contract_tested").flag()&&r.at("runtime_inspected").flag();
    const auto &matrix=j.at("compatibility").at("matrix");if(matrix.kind!=provider_pack_detail::Json::Array||matrix.array.size()>128)throw std::runtime_error("Provider compatibility matrix must contain at most 128 rows");
    for(const auto &row:matrix.array){if(!row.at("physical_qualified").flag())continue;DdcQualificationReceipt q;q.windowsMinBuild=row.at("windows_min_build").integer();q.windowsMaxBuild=row.at("windows_max_build").integer();q.driverVersion=row.at("driver_version").string();q.hardwareId=row.at("hardware_id").string();q.receipt=row.at("receipt").string();if(!q.windowsMinBuild||q.windowsMaxBuild<q.windowsMinBuild||q.driverVersion.empty()||q.hardwareId.empty()||q.hardwareId.find('*')!=std::string::npos||q.hardwareId.find('?')!=std::string::npos||q.receipt.empty())throw std::runtime_error("Incomplete finite provider qualification row");p.matrix.push_back(std::move(q));}
    if(!p.receiptsComplete)throw std::runtime_error("Provider pack software verification receipts are incomplete");
  }catch(const std::exception &e){p.error=e.what();}
  return p;
}
}
