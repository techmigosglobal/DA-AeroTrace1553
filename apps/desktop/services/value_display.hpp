#pragma once

#include <QString>
#include <cmath>

namespace desktop {
inline QString displayValue(double value) {
  if(!std::isfinite(value))return QStringLiteral("—");
  const auto magnitude=std::abs(value);
  const bool scientific=magnitude>=1e7 || (magnitude>0 && magnitude<1e-6);
  auto text=QString::number(value==0?0.:value,scientific?'e':'f',6);
  auto end=text.indexOf('e');if(end<0)end=text.size();
  auto last=end;while(last>0&&text[last-1]=='0')--last;
  if(last>0&&text[last-1]=='.')--last;
  text.remove(last,end-last);
  return text;
}
}
