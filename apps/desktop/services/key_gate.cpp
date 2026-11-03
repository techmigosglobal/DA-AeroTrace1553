#include "key_gate.hpp"
#include <QByteArray>
#include <QCryptographicHash>
#include <QRegularExpression>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <ncrypt.h>
#include <cstring>
#include <vector>
#endif
#ifndef AEROTRACE_KEY_CERT_SHA256
#define AEROTRACE_KEY_CERT_SHA256 ""
#endif
namespace desktop {
bool keyRequired(){return !QString::fromLatin1(AEROTRACE_KEY_CERT_SHA256).isEmpty();}
KeyProof proveApplicationKey(bool interactive) {
  const auto pin=QString::fromLatin1(AEROTRACE_KEY_CERT_SHA256).toLower();
  if(pin.isEmpty())return {false,"Application key enforcement is not configured in this build"};
  if(!QRegularExpression("^[0-9a-f]{64}$").match(pin).hasMatch())return {false,"Invalid application key policy"};
#ifdef Q_OS_WIN
  struct Resources {
    HCERTSTORE store{};PCCERT_CONTEXT cert{};NCRYPT_KEY_HANDLE key{};BOOL freeKey{};BCRYPT_KEY_HANDLE publicKey{};
    ~Resources(){if(publicKey)BCryptDestroyKey(publicKey);if(key && freeKey)NCryptFreeObject(key);if(cert)CertFreeCertificateContext(cert);if(store)CertCloseStore(store,0);}
  } resource;
  resource.store=CertOpenSystemStoreW(0,L"MY");if(!resource.store)return {false,"Cannot open the user certificate store"};
  PCCERT_CONTEXT current=nullptr;
  while((current=CertEnumCertificatesInStore(resource.store,current))) {
    const QByteArray der(reinterpret_cast<const char *>(current->pbCertEncoded),current->cbCertEncoded);
    if(QCryptographicHash::hash(der,QCryptographicHash::Sha256).toHex()==pin.toLatin1()){resource.cert=CertDuplicateCertificateContext(current);CertFreeCertificateContext(current);break;}
  }
  if(!resource.cert)return {false,"Enrolled PIV certificate is unavailable; connect the application key"};
  if(CertVerifyTimeValidity(nullptr,resource.cert->pCertInfo)!=0)return {false,"Application key certificate is outside its validity period"};
  DWORD bytes{};if(!CertGetCertificateContextProperty(resource.cert,CERT_KEY_PROV_INFO_PROP_ID,nullptr,&bytes) || bytes>65536)return {false,"Certificate has no hardware key provider"};
  std::vector<BYTE> provider(bytes);if(!CertGetCertificateContextProperty(resource.cert,CERT_KEY_PROV_INFO_PROP_ID,provider.data(),&bytes))return {false,"Cannot read hardware key provider"};
  const auto info=reinterpret_cast<CRYPT_KEY_PROV_INFO *>(provider.data());
  if(!info->pwszProvName || wcscmp(info->pwszProvName,MS_SMART_CARD_KEY_STORAGE_PROVIDER)!=0)return {false,"Application key must use the Windows Smart Card Key Storage Provider"};
  DWORD spec{};HCRYPTPROV_OR_NCRYPT_KEY_HANDLE handle{};
  const DWORD acquire=CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG|CRYPT_ACQUIRE_NO_HEALING|(interactive?0:CRYPT_ACQUIRE_SILENT_FLAG);
  if(!CryptAcquireCertificatePrivateKey(resource.cert,acquire,nullptr,&handle,&spec,&resource.freeKey))return {false,"Key unavailable or PIN authentication required; select Verify application key"};
  resource.key=handle;if(spec!=CERT_NCRYPT_KEY_SPEC)return {false,"CNG smart-card key required"};
  BYTE challenge[32];if(BCryptGenRandom(nullptr,challenge,sizeof(challenge),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)return {false,"Cannot create a cryptographic challenge"};
  const auto digest=QCryptographicHash::hash(QByteArray("AeroTrace key proof v1\0",23)+QByteArray(reinterpret_cast<char *>(challenge),sizeof(challenge)),QCryptographicHash::Sha256);
  const auto oid=resource.cert->pCertInfo->SubjectPublicKeyInfo.Algorithm.pszObjId;const bool rsa=std::strcmp(oid,szOID_RSA_RSA)==0;
  if(!rsa && std::strcmp(oid,szOID_ECC_PUBLIC_KEY)!=0)return {false,"Use an RSA or ECDSA PIV signing certificate"};
  BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};void *paddingInfo=rsa?&padding:nullptr;const DWORD flags=(rsa?NCRYPT_PAD_PKCS1_FLAG:0)|(interactive?0:NCRYPT_SILENT_FLAG);
  DWORD size{};auto hash=reinterpret_cast<PBYTE>(const_cast<char *>(digest.constData()));
  auto status=NCryptSignHash(resource.key,paddingInfo,hash,digest.size(),nullptr,0,&size,flags);
  if(status!=ERROR_SUCCESS || !size || size>8192)return {false,"Hardware signing failed; reconnect the key and verify its PIN"};
  std::vector<BYTE> signature(size);status=NCryptSignHash(resource.key,paddingInfo,hash,digest.size(),signature.data(),size,&size,flags);
  if(status!=ERROR_SUCCESS)return {false,"Application key challenge failed"};
  if(!CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING,&resource.cert->pCertInfo->SubjectPublicKeyInfo,0,nullptr,&resource.publicKey))return {false,"Cannot import application key public certificate"};
  if(BCryptVerifySignature(resource.publicKey,paddingInfo,hash,digest.size(),signature.data(),size,rsa?BCRYPT_PAD_PKCS1:0)!=0)return {false,"Application key signature is invalid"};
  return {true,"Application key verified by a fresh hardware signature"};
#else
  return {false,"PIV application key verification currently requires Windows"};
#endif
}
}
