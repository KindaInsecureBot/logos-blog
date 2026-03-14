#pragma once
#include <QByteArray>
#include <QString>

struct Keypair {
    QString pubkeyHex;   // 64 hex chars = 32-byte Ed25519 public key
    QString privkeyHex;  // 64 hex chars = 32-byte Ed25519 seed (private key)
};

namespace Crypto {
    // Generate a new Ed25519 keypair
    Keypair generateEd25519Keypair();

    // Sign message with Ed25519 private key (64-char hex seed)
    // Returns 128-char hex signature, or empty string on error
    QString sign(const QString& privkeyHex, const QByteArray& message);

    // Verify Ed25519 signature
    // pubkeyHex: 64-char hex, sigHex: 128-char hex
    bool verify(const QString& pubkeyHex, const QString& sigHex, const QByteArray& message);
}
