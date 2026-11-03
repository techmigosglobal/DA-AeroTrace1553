#include "services/key_gate.hpp"
#include <QCoreApplication>
#include <iostream>
int main(int argc,char **argv){QCoreApplication app(argc,argv);
  if(!desktop::keyRequired()){std::cerr<<"Test build must pin a nonexistent certificate\n";return 1;}
  const auto proof=desktop::proveApplicationKey(false);
  if(proof.valid || proof.message.isEmpty()){std::cerr<<"Missing application key must fail closed\n";return 1;}
  std::cout<<"PASS: pinned-certificate policy rejects missing hardware without a PIN prompt\n";
}
