#include "crypto.h"

#include <openssl/evp.h>

namespace Crypto {

Keypair generateEd25519Keypair()
{
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    if (!ctx) return {};

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return {};
    }

    EVP_PKEY* pkey = nullptr;
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return {};
    }
    EVP_PKEY_CTX_free(ctx);

    size_t pubLen  = 32;
    size_t privLen = 32;
    unsigned char pubRaw[32];
    unsigned char privRaw[32];

    if (EVP_PKEY_get_raw_public_key(pkey, pubRaw, &pubLen) <= 0 ||
        EVP_PKEY_get_raw_private_key(pkey, privRaw, &privLen) <= 0)
    {
        EVP_PKEY_free(pkey);
        return {};
    }
    EVP_PKEY_free(pkey);

    Keypair kp;
    kp.pubkeyHex  = QByteArray(reinterpret_cast<const char*>(pubRaw),  32).toHex();
    kp.privkeyHex = QByteArray(reinterpret_cast<const char*>(privRaw), 32).toHex();
    return kp;
}

QString sign(const QString& privkeyHex, const QByteArray& message)
{
    const QByteArray privRaw = QByteArray::fromHex(privkeyHex.toLatin1());
    if (privRaw.size() != 32) return {};

    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519, nullptr,
        reinterpret_cast<const unsigned char*>(privRaw.constData()), 32);
    if (!pkey) return {};

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) { EVP_PKEY_free(pkey); return {}; }

    if (EVP_DigestSignInit(mdctx, nullptr, nullptr, nullptr, pkey) <= 0) {
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return {};
    }

    size_t sigLen = 64;
    unsigned char sig[64];
    if (EVP_DigestSign(mdctx, sig, &sigLen,
            reinterpret_cast<const unsigned char*>(message.constData()),
            static_cast<size_t>(message.size())) <= 0)
    {
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return {};
    }

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    return QByteArray(reinterpret_cast<const char*>(sig), 64).toHex();
}

bool verify(const QString& pubkeyHex, const QString& sigHex, const QByteArray& message)
{
    const QByteArray pubRaw = QByteArray::fromHex(pubkeyHex.toLatin1());
    const QByteArray sigRaw = QByteArray::fromHex(sigHex.toLatin1());
    if (pubRaw.size() != 32 || sigRaw.size() != 64) return false;

    EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED25519, nullptr,
        reinterpret_cast<const unsigned char*>(pubRaw.constData()), 32);
    if (!pkey) return false;

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) { EVP_PKEY_free(pkey); return false; }

    if (EVP_DigestVerifyInit(mdctx, nullptr, nullptr, nullptr, pkey) <= 0) {
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return false;
    }

    const int ret = EVP_DigestVerify(mdctx,
        reinterpret_cast<const unsigned char*>(sigRaw.constData()), 64,
        reinterpret_cast<const unsigned char*>(message.constData()),
        static_cast<size_t>(message.size()));

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    return ret == 1;
}

} // namespace Crypto
