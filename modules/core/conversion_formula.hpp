#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aero {

class ConversionFormula {
  enum class Op { Constant, Input, Negate, Add, Subtract, Multiply, Divide, Abs, Sqrt, Min, Max, Pow };
  struct Instruction { Op op; double value{}; };
  std::string source;
  std::vector<Instruction> program;

  class Parser {
    std::string_view input;
    std::vector<Instruction> &program;
    static bool digit(char c) { return c >= '0' && c <= '9'; }
    static bool letter(char c) { return c >= 'a' && c <= 'z'; }
    void skipSpace() {
      while (!input.empty() && std::string_view(" \t\r\n").find(input.front()) != std::string_view::npos)
        input.remove_prefix(1);
    }
    bool eat(char c) {
      skipSpace();
      if (input.empty() || input.front() != c) return false;
      input.remove_prefix(1);
      return true;
    }
    bool emit(Op op, double value = 0) {
      if (program.size() == 64) return false;
      program.push_back({op, value});
      return true;
    }
    bool factor(unsigned depth) {
      if (depth > 16) return false;
      if (eat('+')) return factor(depth + 1);
      if (eat('-')) return factor(depth + 1) && emit(Op::Negate);
      if (eat('(')) return expression(depth + 1) && eat(')');
      if (input.empty()) return false;
      if (digit(input.front()) || input.front() == '.') {
        double number{};
        const auto result = std::from_chars(input.data(), input.data() + input.size(), number);
        if (result.ec != std::errc() || !std::isfinite(number)) return false;
        input.remove_prefix(result.ptr - input.data());
        return emit(Op::Constant, number);
      }
      size_t n = 0;
      while (n < input.size() && letter(input[n])) ++n;
      const auto name = input.substr(0, n);
      input.remove_prefix(n);
      if (name == "x") return emit(Op::Input);
      const bool unary = name == "abs" || name == "sqrt";
      const bool binary = name == "min" || name == "max" || name == "pow";
      if ((!unary && !binary) || !eat('(') || !expression(depth + 1)) return false;
      if (binary && (!eat(',') || !expression(depth + 1))) return false;
      if (!eat(')')) return false;
      if (name == "abs") return emit(Op::Abs);
      if (name == "sqrt") return emit(Op::Sqrt);
      if (name == "min") return emit(Op::Min);
      if (name == "max") return emit(Op::Max);
      return emit(Op::Pow);
    }
    bool term(unsigned depth) {
      if (!factor(depth)) return false;
      while (true) {
        if (eat('*')) { if (!factor(depth) || !emit(Op::Multiply)) return false; }
        else if (eat('/')) { if (!factor(depth) || !emit(Op::Divide)) return false; }
        else return true;
      }
    }
    bool expression(unsigned depth) {
      if (!term(depth)) return false;
      while (true) {
        if (eat('+')) { if (!term(depth) || !emit(Op::Add)) return false; }
        else if (eat('-')) { if (!term(depth) || !emit(Op::Subtract)) return false; }
        else return true;
      }
    }

  public:
    Parser(std::string_view text, std::vector<Instruction> &out) : input(text), program(out) {}
    bool parse() {
      if (!expression(0)) return false;
      skipSpace();
      return input.empty();
    }
  };

public:
  bool matches(std::string_view text) const { return source == text; }

  static std::optional<ConversionFormula> compile(std::string_view text) {
    if (text.empty() || text.size() > 256) return {};
    ConversionFormula formula;
    Parser parser(text, formula.program);
    if (!parser.parse()) return {};
    formula.source = text;
    return formula;
  }

  std::optional<double> evaluate(double input) const {
    if (!std::isfinite(input)) return {};
    std::array<double, 64> stack{};
    size_t size = 0;
    for (const auto &instruction : program) {
      switch (instruction.op) {
      case Op::Input: stack[size++] = input; continue;
      case Op::Constant: stack[size++] = instruction.value; continue;
      case Op::Negate: stack[size - 1] = -stack[size - 1]; break;
      case Op::Abs: stack[size - 1] = std::abs(stack[size - 1]); break;
      case Op::Sqrt: stack[size - 1] = std::sqrt(stack[size - 1]); break;
      default: {
        const auto right = stack[--size];
        auto &left = stack[size - 1];
        switch (instruction.op) {
        case Op::Add: left += right; break;
        case Op::Subtract: left -= right; break;
        case Op::Multiply: left *= right; break;
        case Op::Divide: if (right == 0) return {}; left /= right; break;
        case Op::Min: left = std::min(left, right); break;
        case Op::Max: left = std::max(left, right); break;
        case Op::Pow: left = std::pow(left, right); break;
        default: return {};
        }
        break;
      }
      }
      if (!std::isfinite(stack[size - 1])) return {};
    }
    return size == 1 ? std::optional(stack[0]) : std::nullopt;
  }
};
}
