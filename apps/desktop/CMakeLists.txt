find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent)
qt_standard_project_setup()
add_library(aerotrace_diagnostics STATIC services/diagnostic_log.cpp services/diagnostic_log.hpp)
target_link_libraries(aerotrace_diagnostics PUBLIC Qt6::Core)
target_include_directories(aerotrace_diagnostics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
  target_link_libraries(aerotrace_diagnostics PUBLIC dbghelp)
endif()
add_executable(aerotrace_diagnostic_tests tests/diagnostic_tests.cpp)
target_link_libraries(aerotrace_diagnostic_tests PRIVATE aerotrace_diagnostics)
add_test(NAME structured_logging_and_crash_dump COMMAND aerotrace_diagnostic_tests)
set_tests_properties(structured_logging_and_crash_dump PROPERTIES TIMEOUT 45)
set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/forms")
qt_add_executable(aerotrace_desktop
  main.cpp main_window.hpp main_window.cpp ui_helpers.hpp
  widgets/plot.hpp models/record_model.hpp models/rt_record_model.hpp forms/rt_channel_body.ui
  pages/workspace_pages.cpp pages/monitor_pages.cpp pages/session_pages.cpp pages/icd_pages.cpp
  pages/system_pages.cpp tests/gui_checks.cpp tests/performance_qualification.cpp tests/session_gui.cpp
  pages/form_support.cpp pages/document_pages.cpp pages/operator_shell.cpp services/profile_io.cpp
  services/profile_io.hpp forms/document_dialog.ui
  services/task_services.hpp services/task_services.cpp pages/task_pages.cpp forms/task_dialog.ui
  services/key_gate.hpp services/key_gate.cpp pages/key_pages.cpp
  pages/support_pages.cpp services/session_store.cpp services/session_store.hpp pages/hardware_pages.cpp services/ddc_discovery.cpp services/ddc_discovery.hpp services/runtime_paths.hpp services/ui_observer.hpp
  tests/rt_monitor_gui.cpp
  pages/page_catalog.hpp
  forms/welcome_page.ui forms/device_page.ui forms/dashboard_page.ui
  forms/monitor_page.ui forms/inspector_page.ui forms/engineering_page.ui
  forms/plots_page.ui forms/topology_page.ui forms/events_page.ui
  forms/sessions_page.ui forms/replay_page.ui forms/icd_library_page.ui
  forms/icd_editor_page.ui forms/exports_page.ui forms/security_page.ui
  forms/diagnostics_page.ui forms/page_header.ui forms/trace_card.ui
  forms/workspace.ui forms/page_footer.ui
  forms/main_window.ui forms/license_page.ui forms/preferences_page.ui)
target_include_directories(aerotrace_desktop PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_precompile_headers(aerotrace_desktop PRIVATE <QtWidgets> <QtConcurrent>)
string(REPLACE "\\" "/" document_python_path "${AEROTRACE_DOCUMENT_PYTHON}")
target_compile_definitions(aerotrace_desktop PRIVATE
  AEROTRACE_DOCUMENT_PYTHON="${document_python_path}"
  AEROTRACE_DOCUMENT_WORKER="${PROJECT_SOURCE_DIR}/tools/document_worker/worker.py")
target_compile_definitions(aerotrace_desktop PRIVATE AEROTRACE_DIAGNOSTIC_COLLECTOR="${PROJECT_SOURCE_DIR}/scripts/collect-diagnostics.ps1" AEROTRACE_OPERATOR_GUIDE="${PROJECT_SOURCE_DIR}/docs/Operator-Guide.html")
qt_add_resources(aerotrace_desktop styles PREFIX "/" FILES style.qss)
file(GLOB AEROTRACE_ICONS CONFIGURE_DEPENDS "icons/*.svg")
qt_add_resources(aerotrace_desktop operator_icons PREFIX "/" BASE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${AEROTRACE_ICONS})
qt_add_resources(aerotrace_desktop app_icon PREFIX "/icons" BASE "${CMAKE_CURRENT_SOURCE_DIR}/icons" FILES icons/aerotrace.ico)
if(WIN32)
  if(MINGW)
    # Isolate RC from C++ definitions containing quoted paths/spaces (windres preprocessing limitation).
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/app-icon.obj"
      COMMAND "${CMAKE_RC_COMPILER}" -O coff app.rc "${CMAKE_CURRENT_BINARY_DIR}/app-icon.obj"
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      DEPENDS app.rc icons/aerotrace.ico VERBATIM)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/app-icon.obj" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
    target_sources(aerotrace_desktop PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app-icon.obj")
  else()
    enable_language(RC)
    target_sources(aerotrace_desktop PRIVATE app.rc)
  endif()
endif()
target_link_libraries(aerotrace_desktop PRIVATE Qt6::Widgets Qt6::Concurrent aerotrace_application aerotrace_diagnostics)
set(AEROTRACE_KEY_CERT_SHA256 "" CACHE STRING "SHA-256 DER certificate fingerprint of the enrolled application PIV key; empty disables enforcement")
if(NOT AEROTRACE_KEY_CERT_SHA256 STREQUAL "")
  string(LENGTH "${AEROTRACE_KEY_CERT_SHA256}" KEY_PIN_LENGTH)
  if(NOT KEY_PIN_LENGTH EQUAL 64 OR NOT AEROTRACE_KEY_CERT_SHA256 MATCHES "^[0-9a-fA-F]+$")
    message(FATAL_ERROR "AEROTRACE_KEY_CERT_SHA256 must be a 64-digit SHA-256 fingerprint")
  endif()
endif()
target_compile_definitions(aerotrace_desktop PRIVATE AEROTRACE_KEY_CERT_SHA256="${AEROTRACE_KEY_CERT_SHA256}")
if(WIN32)
  target_link_libraries(aerotrace_desktop PRIVATE crypt32 ncrypt bcrypt setupapi cfgmgr32 version)
endif()
add_executable(aerotrace_key_gate_tests tests/key_gate_tests.cpp services/key_gate.cpp)
target_include_directories(aerotrace_key_gate_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aerotrace_key_gate_tests PRIVATE Qt6::Core)
target_compile_definitions(aerotrace_key_gate_tests PRIVATE AEROTRACE_KEY_CERT_SHA256="ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff")
if(WIN32)
  target_link_libraries(aerotrace_key_gate_tests PRIVATE crypt32 ncrypt bcrypt setupapi cfgmgr32 version)
endif()
add_test(NAME application_key_missing_fails_closed COMMAND aerotrace_key_gate_tests)
set_tests_properties(application_key_missing_fails_closed PROPERTIES TIMEOUT 15)
find_path(QWT_INCLUDE_DIR qwt_plot.h HINTS "${PROJECT_SOURCE_DIR}/third_party/qwt/src" PATH_SUFFIXES qwt)
find_library(QWT_LIBRARY NAMES qwt qwt-qt6 HINTS "${PROJECT_SOURCE_DIR}/out/build/qwt-local/lib")
find_library(QWT_DEBUG_LIBRARY NAMES qwtd qwt-qt6d HINTS "${PROJECT_SOURCE_DIR}/out/build/qwt-local/lib")
if(QWT_INCLUDE_DIR AND QWT_LIBRARY)
  target_include_directories(aerotrace_desktop PRIVATE ${QWT_INCLUDE_DIR})
  if(QWT_DEBUG_LIBRARY)
    target_link_libraries(aerotrace_desktop PRIVATE optimized ${QWT_LIBRARY} debug ${QWT_DEBUG_LIBRARY})
  else()
    target_link_libraries(aerotrace_desktop PRIVATE ${QWT_LIBRARY})
  endif()
  target_compile_definitions(aerotrace_desktop PRIVATE AEROTRACE_QWT=1 QWT_DLL)
  get_filename_component(QWT_LIBRARY_DIR "${QWT_LIBRARY}" DIRECTORY)
  if(WIN32 AND EXISTS "${QWT_LIBRARY_DIR}/qwt.dll")
    add_custom_command(TARGET aerotrace_desktop POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${QWT_LIBRARY_DIR}/$<IF:$<AND:$<CONFIG:Debug>,$<BOOL:${QWT_DEBUG_LIBRARY}>>,qwtd.dll,qwt.dll>" "$<TARGET_FILE_DIR:aerotrace_desktop>"
      COMMENT "Deploying the Qwt runtime beside the desktop executable")
  endif()
endif()
set_target_properties(aerotrace_desktop PROPERTIES WIN32_EXECUTABLE TRUE)
add_test(NAME gui_automation_workflow COMMAND aerotrace_desktop --automation-test ${CMAKE_BINARY_DIR}/test-results/automation)
add_test(NAME gui_rt_monitor_workflow COMMAND aerotrace_desktop --rt-monitor-test ${CMAKE_BINARY_DIR}/test-results/rt-monitor)
set_tests_properties(gui_rt_monitor_workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45)
set_tests_properties(gui_automation_workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
add_test(NAME gui_workflows COMMAND aerotrace_desktop --workflow-test ${CMAKE_BINARY_DIR}/test-results)
set_tests_properties(gui_workflows PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
add_test(NAME gui_live_pipeline_stress COMMAND aerotrace_desktop --performance-test ${CMAKE_BINARY_DIR}/test-results)
set_tests_properties(gui_live_pipeline_stress PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
add_test(NAME gui_operator_workflow COMMAND aerotrace_desktop --operator-workflow-test ${CMAKE_BINARY_DIR}/test-results/operator)
set_tests_properties(gui_operator_workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45)
add_test(NAME gui_demo_icd_live COMMAND aerotrace_desktop --demo-workflow-test ${CMAKE_BINARY_DIR}/test-results/demo ${PROJECT_SOURCE_DIR}/examples/demo-icd/AeroTrace-Demo-ICD.pdf)
set_tests_properties(gui_demo_icd_live PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 150)

add_executable(aerotrace_session_tests tests/session_tests.cpp services/session_store.cpp services/ddc_discovery.cpp)
target_include_directories(aerotrace_session_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aerotrace_session_tests PRIVATE Qt6::Core aerotrace_application aerotrace_diagnostics)
if(WIN32)
  target_link_libraries(aerotrace_session_tests PRIVATE setupapi cfgmgr32 version advapi32)
endif()
add_test(NAME full_session_export_indexed_replay COMMAND aerotrace_session_tests)
add_executable(aerotrace_session_export_cli tests/session_export_cli.cpp services/session_store.cpp)
target_include_directories(aerotrace_session_export_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aerotrace_session_export_cli PRIVATE Qt6::Core aerotrace_application aerotrace_diagnostics)
set_tests_properties(full_session_export_indexed_replay PROPERTIES TIMEOUT 120)

add_test(NAME gui_session_stop_save_and_layout COMMAND aerotrace_desktop --session-workflow-test ${CMAKE_BINARY_DIR}/test-results/session)
set_tests_properties(gui_session_stop_save_and_layout PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)

if(TARGET aerotrace_ddc_test_runtime)
  add_test(NAME gui_hardware_connection_recording COMMAND aerotrace_desktop --hardware-connection-test "${CMAKE_BINARY_DIR}/test-results/hardware-connection" "$<TARGET_FILE:aerotrace_ddc_test_runtime>")
  set_tests_properties(gui_hardware_connection_recording PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
  set_property(TEST gui_hardware_connection_recording APPEND PROPERTY ENVIRONMENT "AEROTRACE_DDC_PROVIDER=$<TARGET_FILE:aerotrace_ddc_fixture_provider>")
endif()
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(aerotrace_model_tests tests/model_tests.cpp)
target_include_directories(aerotrace_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aerotrace_model_tests PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Test aerotrace_acquisition)
add_test(NAME qt_model_consistency COMMAND aerotrace_model_tests)
set_tests_properties(qt_model_consistency PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
