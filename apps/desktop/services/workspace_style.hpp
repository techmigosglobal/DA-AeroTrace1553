#pragma once
#include <QtWidgets>
namespace desktop {
inline QIcon workspaceIcon(const QString &name,const QColor &ink=QColor("#315670")) {
  auto pixmap=QIcon(":/icons/"+name+".svg").pixmap(20,20);QPainter painter(&pixmap);painter.setCompositionMode(QPainter::CompositionMode_SourceIn);painter.fillRect(pixmap.rect(),ink);painter.end();return QIcon(pixmap);
}
inline void workspaceState(QWidget *widget,const char *key,const QVariant &value) {
  if(widget->property(key)==value)return;widget->setProperty(key,value);widget->style()->unpolish(widget);widget->style()->polish(widget);widget->update();
}
inline void polishWorkspace(QWidget *root) {
  root->setProperty("workspacePolish",true);
  for(auto button:root->findChildren<QPushButton *>()) {
    const auto text=button->text().toLower();QString icon;
    if(text.contains("manager"))icon="settings";
    else if(text.contains("browse")||text.contains("folder")||text.startsWith("open"))icon="folder";
    else if(text.contains("save")||text.contains("csv"))icon="download";
    else if(text.contains("setup")||text.contains("manager")||text.contains("details"))icon="settings";
    else if(text.contains("start")||text.contains("simulation"))icon="play";
    else if(text.contains("scan"))icon="search";
    else if(text.contains("tile")||text.contains("column"))icon="grid";
    else if(text.contains("cascade"))icon="file";
    else if(text.contains("inspect"))icon="search";
    else if(text.contains("engineering"))icon="sliders";
    else if(text.contains("plot"))icon="chart";
    if(!icon.isEmpty()){button->setIcon(workspaceIcon(icon,button->property("primary").toBool()?QColor("#ffffff"):QColor("#315670")));button->setIconSize({18,18});}
    if(button->toolTip().isEmpty())button->setToolTip(button->text().remove('&'));
  }
  for(auto table:root->findChildren<QTableView *>()){table->setMouseTracking(true);table->setShowGrid(false);table->setTextElideMode(Qt::ElideRight);}
}
}
