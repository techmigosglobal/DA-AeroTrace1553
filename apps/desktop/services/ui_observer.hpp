#pragma once
#include "diagnostic_log.hpp"
#include <QtWidgets>
inline void observeUiActions(QWidget *root) {
  for(auto button:root->findChildren<QAbstractButton *>()) {
    if(button->property("aeroObserved").toBool())continue;button->setProperty("aeroObserved",true);
    QObject::connect(button,&QAbstractButton::clicked,button,[button](bool checked){applicationEvent("ui_action",button->text(),{{"object",button->objectName()},{"window",button->window()->windowTitle()},{"checked",checked}});});
  }
}
