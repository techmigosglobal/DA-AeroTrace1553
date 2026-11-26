#pragma once

#include "core.hpp"
#include "conversion_formula.hpp"
#include <bit>
#include <cmath>

namespace aero {
// Built once for validated live rules. Preview and file conversion use the
// same extraction and arithmetic, with validation at their public entry point.
class FieldEncoding {
  enum class Type { Unsigned, Signed, Float32, Float64, Bcd };
  Type type;
  unsigned shift,width;
  std::uint64_t mask,sign;
  bool lswFirst;
  double scale,offset;
  bool custom;
  std::shared_ptr<const ConversionFormula> formula;
public:
  unsigned wordCount;
  explicit FieldEncoding(const Signal &signal):
    type(signal.numericType=="float32"?Type::Float32:signal.numericType=="float64"?Type::Float64:
         signal.numericType=="bcd"?Type::Bcd:signal.signedValue?Type::Signed:Type::Unsigned),
    shift(signal.lsb),width(signal.width),
    mask(width==64?~std::uint64_t(0):(std::uint64_t(1)<<width)-1),
    sign(std::uint64_t(1)<<(width-1)),lswFirst(signal.leastSignificantWordFirst),
    scale(signal.scale),offset(signal.offset),custom(!signal.conversionFormula.empty()),
    formula(signal.preparedFormula),wordCount((shift+width+15)/16) {
    if(custom && (!formula || !formula->matches(signal.conversionFormula))) {
      auto compiled=ConversionFormula::compile(signal.conversionFormula);
      formula=compiled ? std::make_shared<const ConversionFormula>(std::move(*compiled)) : nullptr;
    }
  }

  std::optional<double> convert(std::span<const std::uint16_t> words) const {
    if(words.size()<wordCount)return {};
    std::uint64_t bits=words.front();
    if(wordCount>1) {
      bits=0;
      for(unsigned i=0;i<wordCount;++i) {
        const auto index=lswFirst?i:wordCount-1-i;
        bits|=std::uint64_t(words[index])<<(i*16);
      }
    }
    bits=(bits>>shift)&mask;
    double value{};
    switch(type) {
      case Type::Float32: value=std::bit_cast<float>(std::uint32_t(bits));break;
      case Type::Float64: value=std::bit_cast<double>(bits);break;
      case Type::Signed:
        if(bits&sign)bits|=~mask;
        value=static_cast<double>(std::bit_cast<std::int64_t>(bits));break;
      case Type::Unsigned: value=static_cast<double>(bits);break;
      case Type::Bcd:
        for(double place=1;bits;place*=10,bits>>=4) {
          const auto digit=bits&15;
          if(digit>9)return {};
          value+=digit*place;
        }
        break;
    }
    if(custom)return formula?formula->evaluate(value):std::nullopt;
    value=value*scale+offset;
    return std::isfinite(value)?std::optional(value):std::nullopt;
  }
};
}
