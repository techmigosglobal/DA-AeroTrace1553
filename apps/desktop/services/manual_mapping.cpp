#include "manual_mapping.hpp"
#include "protocol.hpp"
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <array>
#include <cmath>
#include <limits>

namespace desktop {
namespace {
bool parseHexWordToken(QString token, std::uint16_t &out) {
  token = token.trimmed();
  if (token.startsWith("0x", Qt::CaseInsensitive)) token.remove(0, 2);
  if (token.isEmpty() || token.size() > 4) return false;
  bool ok = false;
  const auto val = token.toUInt(&ok, 16);
  if (!ok || val > 0xFFFFu) return false;
  out = std::uint16_t(val);
  return true;
}

bool parseHexWords(const QString &rawText, unsigned span, std::vector<std::uint16_t> &out, QString &normalizedKey) {
  out.clear();
  normalizedKey.clear();
  static const QRegularExpression separator("[,;\\s:\\-]+");
  auto tokens = rawText.trimmed().split(separator, Qt::SkipEmptyParts);
  // Allow contiguous multi-word hex strings (e.g. "3F800000" or "0x3F800000" for 2 words).
  if (tokens.size() == 1 && span > 1) {
    auto compact = tokens.front();
    if (compact.startsWith("0x", Qt::CaseInsensitive)) compact.remove(0, 2);
    static const QRegularExpression hexDigits("^[0-9a-f]+$", QRegularExpression::CaseInsensitiveOption);
    if (hexDigits.match(compact).hasMatch() && compact.size() > 4 && compact.size() <= int(span * 4)) {
      if (compact.size() % 4 != 0) compact.prepend(QString(4 - compact.size() % 4, '0'));
      QStringList chunks;
      for (int pos = 0; pos < compact.size(); pos += 4) chunks << compact.mid(pos, 4);
      while (chunks.size() < int(span)) chunks.prepend("0000");
      tokens = chunks;
    }
  }
  if (tokens.size() != int(span)) return false;
  QStringList normalized;
  out.reserve(span);
  for (const auto &token : tokens) {
    std::uint16_t value = 0;
    if (!parseHexWordToken(token, value)) return false;
    out.push_back(value);
    normalized << QString::number(value, 16);
  }
  normalizedKey = normalized.join(' ');
  return true;
}

double parseNumberExpression(const QString &text, bool &ok) {
  const auto trimmed = text.trimmed();
  double direct = trimmed.toDouble(&ok);
  if (ok) return direct;
  static const QRegularExpression power("^2\\^\\(?(-?\\d+(?:\\.\\d+)?)\\)?$");
  if (const auto m = power.match(trimmed); m.hasMatch()) {
    const double exp = m.captured(1).toDouble(&ok);
    if (ok) return std::pow(2.0, exp);
  }
  const auto slash = trimmed.indexOf('/');
  if (slash > 0) {
    bool numOk = false, denOk = false;
    const double num = trimmed.left(slash).trimmed().toDouble(&numOk);
    const double den = trimmed.mid(slash + 1).trimmed().toDouble(&denOk);
    if (numOk && denOk && den != 0.0) {
      ok = true;
      return num / den;
    }
  }
  ok = false;
  return 0.0;
}

unsigned wordBitMask(const aero::Signal &signal, unsigned word) {
  const auto span = (signal.lsb + signal.width + 15) / 16;
  if (word < signal.word || word >= signal.word + span) return 0u;
  const auto order = signal.leastSignificantWordFirst ? word - signal.word : span - 1 - (word - signal.word);
  unsigned mask = 0u;
  for (unsigned bit = 0; bit < 16; ++bit) {
    const unsigned fieldBit = order * 16 + bit;
    if (fieldBit >= signal.lsb && fieldBit < signal.lsb + signal.width) mask |= 1u << bit;
  }
  return mask;
}
} // namespace

QJsonArray normalizeManualCommands(const QJsonArray &commands) {
  QJsonArray normalized;
  for (const auto &entry : commands) {
    auto cmd = entry.toObject();
    if (cmd.value("word_base").toInt(-1) != 0) {
      QJsonArray nextFields;
      for (const auto &fEntry : cmd["fields"].toArray()) {
        auto field = fEntry.toObject();
        const int w = field["word"].toInt();
        field["word"] = w > 0 ? w - 1 : 0;
        nextFields.append(field);
      }
      cmd["fields"] = nextFields;
      cmd["word_base"] = 0;
    }
    normalized.append(cmd);
  }
  return normalized;
}

QString manualRouteLabel(const QJsonObject &command) {
  const auto count = command["count"].toInt();
  const auto rt = command["rt"].toInt();
  const auto sa = command["sa"].toInt();
  const bool transmit = command["tx"].toBool();
  const auto word = (rt << 11) | (int(transmit) << 10) | (sa << 5) | (count & 31);
  auto label = QString("RT %1 / SA %2 / %3 / %4 words · 0x%5")
    .arg(rt).arg(sa).arg(transmit ? "Transmit" : "Receive").arg(count)
    .arg(word, 4, 16, QChar('0'));
  const int matchWord = command.value("match_word").toInt(-1);
  if (matchWord >= 0) {
    std::uint16_t tag = 0;
    const auto mv = command.value("match_value");
    if (mv.isString()) parseHexWordToken(mv.toString(), tag);
    else tag = std::uint16_t(mv.toInt());
    label += QString(" · W%1=0x%2").arg(matchWord).arg(tag, 4, 16, QChar('0'));
  }
  return label;
}

bool overlappingManualRoute(const aero::Signal &a, const aero::Signal &b) {
  const bool sameChannel = !a.channel || !b.channel || a.channel == b.channel;
  const bool sameBus = a.bus < 0 || b.bus < 0 || a.bus == b.bus;
  const bool sameCount = !a.expectedWordCount || !b.expectedWordCount || a.expectedWordCount == b.expectedWordCount;
  const bool distinctTag = a.matchWord >= 0 && b.matchWord >= 0 && a.matchWord == b.matchWord && a.matchValue != b.matchValue;
  return a.rt == b.rt && a.sa == b.sa && a.transmit == b.transmit && sameCount && sameChannel && sameBus && !distinctTag;
}

ManualMappingResult checkManualMapping(const QJsonObject &command) {
  ManualMappingResult result;
  const auto id = command["id"].toString();
  const auto subsystem = command["subsystem"].toString().trimmed();
  const auto rt = command["rt"].toInt(-1), sa = command["sa"].toInt(-1), count = command["count"].toInt(-1);
  const auto channel = command["channel"].toInt(-1), bus = command["bus"].toInt(-2);
  if (id.isEmpty() || subsystem.isEmpty() || rt < 0 || rt > 30 || sa < 1 || sa > 30 ||
      count < 1 || count > 32 || channel < 0 || channel > 2 || bus < -1 || bus > 1 || !command["tx"].isBool()) {
    result.error = "Enter a subsystem and a normal data command with RT 0–30, SA 1–30 and 1–32 words.";
    return result;
  }
  const int matchWord = command.value("match_word").toInt(-1);
  if (matchWord < -1 || matchWord >= count) {
    result.error = QString("Frame tag word must be disabled (-1) or Word 0 to Word %1.").arg(count - 1);
    return result;
  }
  std::uint16_t matchValue = 0;
  if (matchWord >= 0) {
    const auto mv = command.value("match_value");
    if (mv.isDouble()) {
      const auto v = mv.toInt(-1);
      if (v < 0 || v > 65535) { result.error = "Enter a 16-bit frame tag value (0000–FFFF)."; return result; }
      matchValue = std::uint16_t(v);
    } else if (!parseHexWordToken(mv.toString(), matchValue)) {
      result.error = "Enter a 16-bit hex frame tag value (e.g. 9999 or 0x2226).";
      return result;
    }
  }
  const auto fields = command["fields"].toArray();
  if (fields.isEmpty() || fields.size() > 512) { result.error = "Enter 1–512 engineering fields."; return result; }
  std::array<unsigned, 32> usedBitMasks{};
  QSet<QString> names, ids;
  constexpr auto missing = std::numeric_limits<double>::quiet_NaN();
  for (const auto &entry : fields) {
    const auto field = entry.toObject();
    const auto key = field["id"].toString(), name = field["name"].toString().trimmed();
    const int word = field["word"].toInt(-1);
    const int lsb = field["lsb"].toInt(0), width = field["width"].toInt(16);
    const auto type = field["type"].toString(), order = field["order"].toString();
    const auto scale = field["scale"].toString().trimmed(), offset = field["offset"].toString().trimmed();
    const auto tolerance = field["tolerance"].toString().trimmed();
    const bool allowOverlap = field["allow_overlap"].toBool(false);
    bool scaleOk = true, offsetOk = true, toleranceOk = true;
    aero::Signal signal;
    signal.id = ("manual/" + id + "/" + key).toStdString();
    signal.icdSetId = "manual";
    signal.subsystem = subsystem.toStdString();
    signal.messageId = id.toStdString();
    signal.sourceSignalId = name.toStdString();
    signal.sourceDocument = "Manual mapping";
    signal.unit = field["unit"].toString().trimmed().toStdString();
    signal.rt = unsigned(rt);
    signal.sa = unsigned(sa);
    signal.expectedWordCount = unsigned(count);
    signal.channel = unsigned(channel);
    signal.bus = bus;
    signal.transmit = command["tx"].toBool();
    signal.matchWord = matchWord;
    signal.matchValue = matchValue;
    signal.word = word >= 0 ? unsigned(word) : 32;
    signal.lsb = lsb >= 0 ? unsigned(lsb) : 16;
    signal.width = width > 0 ? unsigned(width) : 0;
    signal.numericType = (type == "float32" || type == "float64" || type == "bcd") ? type.toStdString() : "integer";
    signal.signedValue = (type == "signed");
    signal.leastSignificantWordFirst = (order == "LSW first");
    signal.scale = scale.isEmpty() ? 1. : parseNumberExpression(scale, scaleOk);
    signal.offset = offset.isEmpty() ? 0. : parseNumberExpression(offset, offsetOk);
    signal.conversionFormula = field["formula"].toString().trimmed().toStdString();
    aero::prepareConversion(signal);
    const auto allowed = tolerance.isEmpty() ? 0. : tolerance.toDouble(&toleranceOk);
    QString problem;
    if (name.isEmpty() || key.isEmpty() || names.contains(name.toCaseFolded()) || ids.contains(key))
      problem = "Enter a unique field name.";
    else if (word < 0 || word >= count)
      problem = QString("Enter Word 0 to Word %1 (0-based).").arg(count - 1);
    else if (type != "signed" && type != "unsigned" && type != "float32" && type != "float64" && type != "bcd")
      problem = "Select unsigned, signed, float32, float64 or bcd.";
    else if (!scaleOk || !offsetOk || !std::isfinite(signal.scale) || !std::isfinite(signal.offset))
      problem = "Enter a finite scale and offset (or 2^-N / a/b).";
    else if (field["custom"].toBool() && signal.conversionFormula.empty())
      problem = "Enter a custom formula.";
    else if ((signal.numericType == "integer" || signal.numericType == "bcd") && width > 32)
      problem = "Manual integer/BCD fields support up to 32 bits.";
    else if (const auto validation = aero::validate(signal); !validation.empty())
      problem = QString::fromStdString(validation);
    if (problem.isEmpty() && !allowOverlap) {
      for (unsigned w = signal.word; w < signal.word + (signal.lsb + signal.width + 15) / 16 && w < 32; ++w) {
        const unsigned mask = wordBitMask(signal, w);
        if (usedBitMasks[w] & mask) { problem = "Field overlaps another mapped bit (enable 'Allow bit overlap' if intentional)."; break; }
      }
      if (problem.isEmpty()) {
        for (unsigned w = signal.word; w < signal.word + (signal.lsb + signal.width + 15) / 16 && w < 32; ++w)
          usedBitMasks[w] |= wordBitMask(signal, w);
      }
    }
    const auto span = problem.isEmpty() ? (signal.lsb + signal.width + 15) / 16 : 0;
    if (problem.isEmpty() && span > 1 && order != "MSW first" && order != "LSW first")
      problem = "Select LSW first or MSW first for this multiword field.";
    if (problem.isEmpty() && (!toleranceOk || !std::isfinite(allowed) || allowed < 0))
      problem = "Enter a nonnegative finite tolerance.";

    QJsonArray examples;
    for (const auto &exEntry : field["examples"].toArray()) {
      const auto obj = exEntry.toObject();
      if (!obj["raw"].toString().trimmed().isEmpty() || !obj["expected"].toVariant().toString().trimmed().isEmpty())
        examples.append(obj);
    }
    const auto singleRaw = field["raw"].toString().trimmed();
    const auto singleExpected = field["expected"].toVariant().toString().trimmed();
    if (examples.isEmpty() && (!singleRaw.isEmpty() || !singleExpected.isEmpty()))
      examples.append(QJsonObject{{"raw", field["raw"]}, {"expected", field["expected"]}});

    QStringList previews;
    if (problem.isEmpty() && examples.size() > 16)
      problem = "Maximum 16 examples per field.";
    if (problem.isEmpty() && examples.isEmpty()) {
      previews << "Ready · optional check skipped";
    } else if (problem.isEmpty()) {
      QSet<QString> checkedWords;
      for (const auto &exEntry : examples) {
        if (!problem.isEmpty()) break;
        const auto example = exEntry.toObject();
        const auto rawText = example["raw"].toString().trimmed();
        const auto expText = example["expected"].toVariant().toString().trimmed();
        if (rawText.isEmpty() || expText.isEmpty()) {
          problem = "Enter both raw hex and expected value, or clear both to skip checking.";
          break;
        }
        bool expectedOk = false;
        const auto expected = example["expected"].toVariant().toDouble(&expectedOk);
        if (!expectedOk || !std::isfinite(expected)) { problem = "Enter a finite expected engineering value."; break; }
        std::vector<std::uint16_t> raw;
        QString normalizedKey;
        if (!parseHexWords(rawText, span, raw, normalizedKey)) {
          problem = QString("Enter %1 16-bit hex word(s) (e.g. %2).")
            .arg(span).arg(span == 1 ? "0064" : "3F80 0000 or 3F800000");
          break;
        }
        if (checkedWords.contains(normalizedKey)) { problem = "Use different raw words for each example."; break; }
        checkedWords.insert(normalizedKey);
        const auto record = aero::previewSignalRecord(signal, raw, channel ? channel : 1, bus < 0 ? 0 : bus);
        const auto decoded = aero::decode(record, signal);
        const auto difference = decoded ? std::abs(*decoded - expected) : missing;
        const bool passed = std::isfinite(difference) && difference <= allowed;
        previews << QString("%1 · Expected %2 · Actual %3")
          .arg(passed ? "Pass" : "Fail", QString::number(expected, 'g', 15), decoded ? QString::number(*decoded, 'g', 15) : "—");
        if (!passed) problem = "Example mismatch: check word order, scale, offset, or expected value.";
      }
    }
    if (problem.isEmpty()) result.rules.push_back(std::move(signal));
    else if (result.error.isEmpty()) result.error = QString("%1: %2").arg(name.isEmpty() ? QString("Field") : name, problem);
    if (!problem.isEmpty()) previews << problem;
    result.fields.append(previews.join(" | "));
    names.insert(name.toCaseFolded());
    ids.insert(key);
  }
  return result;
}
} // namespace desktop
