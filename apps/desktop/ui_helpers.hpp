#pragma once
#include "core.hpp"
#include "capture_time.hpp"
#include "services/diagnostic_log.hpp"
#include "pages/page_catalog.hpp"
#include <QtConcurrent>
#include <QtWidgets>
#include <array>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#ifdef AEROTRACE_QWT
#include <qwt_picker_machine.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_grid.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_picker.h>
#include <qwt_plot_zoomer.h>
#include <qwt_scale_draw.h>
#include <qwt_text.h>
#endif

inline const QStringList names=[] {
  QStringList result;for(const auto &page:desktop::pages) result << QString::fromUtf8(page.title);return result;
}();
static QString hexword(unsigned w) {
  return QString("%1").arg(w, 4, 16, QChar('0')).toUpper();
}
static QString words(const aero::Record &r) {
  QStringList l;
  for (auto w : r.words)
    l << hexword(w);
  return l.join(' ');
}
static QString stamp(std::uint64_t ns) {
  return QString::fromStdString(aero::formatCardTime(ns));
}
static QLabel *label(QString text, QString id = {}) {
  auto l = new QLabel(text);
  l->setObjectName(id);
  l->setWordWrap(true);
  return l;
}
