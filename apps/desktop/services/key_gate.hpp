#pragma once
#include <QString>
namespace desktop {
// The trusted certificate fingerprint is a release-build policy, never a mutable user preference.
bool keyRequired();
struct KeyProof {bool valid{};QString message;};
KeyProof proveApplicationKey(bool interactive);
}
