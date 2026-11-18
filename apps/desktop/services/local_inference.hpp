#pragma once
#include <QtCore>
#include <QtNetwork>
#include <functional>
#include <memory>

namespace desktop {
// One CPU-only server owned by the desktop, separate from PDF/OCR workers.
class LocalInference : public QObject {
public:
  using Ready=std::function<void(QString,QString,QString)>;
  struct Options {
    QString assetsDirectory,program;
    std::function<bool(quint64)> availableMemory;
  };
  explicit LocalInference(QObject *parent=nullptr);
  LocalInference(QObject *parent,Options options);
  ~LocalInference() override;
  void acquire(bool acquisitionActive,Ready ready);
  void release();
  void cancel();
  void pauseForCapture();
  qint64 processId() const {return server.processId();}
  QString status() const {return state;}
private:
  Options options;
  QString assetDirectory() const;
  bool hasMemory(quint64 minimum=3ull*1024*1024*1024) const;
  QProcess server;
  QNetworkAccessManager network;
  QTimer health,idle,pressure;
  QElapsedTimer startup;
  QTemporaryFile key;
  std::shared_ptr<void> resourceJob;
  Ready pending;
  QString endpoint,token,state{"Offline model unloaded"};
  QByteArray log;
  bool busy{},checking{},readyState{},stopping{};
  quint64 generation{};
  void finish(QString error={});
  void stop();
  void start();
  static bool memoryAvailable(quint64 minimum=3ull*1024*1024*1024);
};
}
