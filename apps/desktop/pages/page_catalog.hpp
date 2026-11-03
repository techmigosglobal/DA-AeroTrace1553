#pragma once
#include <array>

namespace desktop {
struct PageDefinition {
  const char *title;
  const char *form;
  const char *reference;
};
inline constexpr std::array<PageDefinition,18> pages{{
  {"License Gate","license_page.ui","01_License_Gate.png"},
  {"Welcome","welcome_page.ui","02_Welcome.png"},
  {"Device Setup","device_page.ui","03_Device_Setup.png"},
  {"Capture Dashboard","dashboard_page.ui","04_Capture_Dashboard.png"},
  {"Live Bus Monitor","monitor_page.ui","05_Live_Bus_Monitor.png"},
  {"Message Inspector","inspector_page.ui","06_Message_Inspector.png"},
  {"Engineering Values","engineering_page.ui","07_Engineering_Values.png"},
  {"Live Plot Analysis","plots_page.ui","08_Live_Plot_Analysis.png"},
  {"Bus Topology","topology_page.ui","09_Bus_Topology.png"},
  {"Events & Alerts","events_page.ui","10_Events_and_Alerts.png"},
  {"Session Library","sessions_page.ui","11_Session_Library.png"},
  {"Replay Workbench","replay_page.ui","12_Replay_Workbench.png"},
  {"ICD Library","icd_library_page.ui","13_ICD_Library.png"},
  {"ICD Signal Editor","icd_editor_page.ui","14_ICD_Signal_Editor.png"},
  {"Exports & Reports","exports_page.ui","15_Exports_and_Reports.png"},
  {"License & Security","security_page.ui","16_License_and_Security.png"},
  {"Preferences","preferences_page.ui","17_Preferences.png"},
  {"Help & Diagnostics","diagnostics_page.ui","18_Help_and_Diagnostics.png"}
}};
}
