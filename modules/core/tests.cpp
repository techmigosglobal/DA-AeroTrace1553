#include "core.hpp"
#include "capture_time.hpp"
#include <iostream>
#include <stdexcept>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::runtime_error(#x);                                            \
  } while (0)
int main() {
  try {
    CHECK(aero::crc32({})==0);
    CHECK(aero::crc32({'1','2','3','4','5','6','7','8','9'})==0xcbf43926);
    for(unsigned size:{1u,16u,81u,1024u}){std::vector<std::uint8_t> bytes;for(unsigned n=0;n<size;++n)bytes.push_back(std::uint8_t(n*73));std::uint32_t c=~0u;for(auto b:bytes){c^=b;for(unsigned bit=0;bit<8;++bit)c=(c>>1)^(0xedb88320u&-(c&1));}CHECK(aero::crc32(bytes)==~c);}
    CHECK(aero::formatCardTime(1234567890)=="00:00:01.234.567");
    CHECK(aero::formatCardTime(90000123456789ull)=="25:00:00.123.456");
    CHECK(aero::formatCardTime(360000000000000ull)=="100:00:00.000.000");
    aero::RelativeClock48 rtc;const auto range=aero::RelativeClock48::range;
    CHECK(rtc.extend(range-2)==range-2 && rtc.extend(1)==range+1);
    CHECK(rtc.extend(range-1)==range-1 && rtc.extend(2)==range+2 && rtc.wraps==1);
    aero::ClockStats clock;for(auto ns:{100ull,200ull,200ull,50ull,1000ull}) clock.observe(ns);
    CHECK(clock.count==5 && clock.backwards==1 && clock.repeated==1 && clock.maxForwardGapNs==950);
    CHECK(aero::jsonString("a\n\"b")=="\"a\\u000a\\\"b\"");
    auto c = aero::command(0x1860);
    CHECK(c.rt == 3 && c.sa == 3 && c.count == 32 && !c.transmit);
    CHECK(aero::command(0xf800).broadcast);
    CHECK(aero::command(0x1821).count == 1);
    CHECK(aero::command(0x1800).mode);
    CHECK(aero::command(0x1fe1).sa == 31);
    auto r = aero::synthetic(0);
    aero::Signal s{"TEMP", "degC"};
    CHECK(aero::decode(r, s).value() == 10.0);
    r.words[0] = 0xffff;
    s.signedValue = true;
    s.scale = 1;
    CHECK(aero::decode(r, s).value() == -1);
    s.width = 65;
    CHECK(!aero::decode(r, s));
    s.width = 16;
    s.lsb = 16;
    CHECK(!aero::validate(s).empty());
    s.lsb = 0;
    s.width = 16;
    r.flags = 1;
    CHECK(!aero::decode(r, s));
    auto path =
        std::filesystem::temp_directory_path() / "aerotrace-core-test.atd";
    aero::Recorder writer;
    CHECK(writer.open(path));
    std::vector<aero::Record> source;
    for (int i = 0; i < 1000; ++i) {
      source.push_back(aero::synthetic(i));
      CHECK(writer.append(source.back()));
    }
    CHECK(writer.close());
    auto replay = aero::read(path);
    CHECK(replay.error.empty() && !replay.recovered &&
          replay.records == source);
    auto batchPath=path;batchPath+=".batch";
    CHECK(writer.open(batchPath));CHECK(writer.appendBatch(source));CHECK(writer.close());
    CHECK(aero::read(batchPath).records==source);
    {std::ifstream first(path,std::ios::binary),second(batchPath,std::ios::binary);
      CHECK(std::string(std::istreambuf_iterator<char>(first),{})==std::string(std::istreambuf_iterator<char>(second),{}));}
    std::filesystem::remove(batchPath);
    auto size = std::filesystem::file_size(path);
    std::filesystem::resize_file(path, size - 3);
    auto recovered = aero::read(path);
    CHECK(recovered.recovered && recovered.records.size() == 999);
    CHECK(recovered.records.front() == source.front());
    CHECK(writer.open(path));
    CHECK(writer.append(source.front()));
    CHECK(writer.close());
    {
      std::fstream corrupt(path,
                           std::ios::in | std::ios::out | std::ios::binary);
      corrupt.seekp(25);
      char value = 0x7f;
      corrupt.write(&value, 1);
    }
    auto badCrc = aero::read(path);
    CHECK(badCrc.recovered && badCrc.records.empty());
    {
      std::ofstream invalid(path, std::ios::binary | std::ios::trunc);
      invalid << "WRONG";
    }
    CHECK(!aero::read(path).error.empty());
    std::filesystem::remove(path);
    std::cout << "PASS: command fields, signed ICD, invalid quality, "
                 "1000-record exact replay, CRC tail recovery\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
