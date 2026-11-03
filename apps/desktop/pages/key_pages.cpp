#include "main_window.hpp"
#include "services/key_gate.hpp"

void Window::setupKeyGate() {
  if(!desktop::keyRequired())return;
  applicationKeyMessage="Connect the enrolled PIV application key and verify";
  auto timer=new QTimer(this);timer->setInterval(1000);
  connect(timer,&QTimer::timeout,this,[this] {
    // A hung provider never extends the permission lease.
    if(applicationKeyVerified && applicationKeyClock.elapsed()>10000)lockForApplicationKey("Application key proof expired");
    if(applicationKeyVerified && applicationKeyClock.elapsed()>=5000 && !applicationKeyChecking)verifyApplicationKey(false);
  });timer->start();
}
void Window::lockForApplicationKey(const QString &message) {
  applicationKeyVerified=false;applicationKeyMessage=message;automaticHeld=true;configurationRetry.stop();
  if(documentDialog)documentDialog->close();if(taskDialog)taskDialog->close();
  if(unlocked || connecting || backend.acquisition.active())removeKey();
  else navigate(0);
  toast(message);
}
void Window::verifyApplicationKey(bool interactive) {
  if(applicationKeyChecking || !desktop::keyRequired())return;
  applicationKeyChecking=true;auto watcher=new QFutureWatcher<desktop::KeyProof>(this);
  connect(watcher,&QFutureWatcher<desktop::KeyProof>::finished,this,[this,watcher,interactive]{
    const auto proof=watcher->result();applicationKeyChecking=false;applicationKeyMessage=proof.message;
    if(!proof.valid)lockForApplicationKey(proof.message);
    else {
      applicationKeyVerified=true;applicationKeyClock.restart();
      if(interactive){unlocked=true;automaticHeld=true;navigate(pendingPage==0?4:pendingPage);toast("Application key verified. Start capture explicitly.");}
    }
    watcher->deleteLater();
  });watcher->setFuture(QtConcurrent::run([interactive]{try{return desktop::proveApplicationKey(interactive);}catch(...){return desktop::KeyProof{false,"Application key verification failed"};}}));
}
