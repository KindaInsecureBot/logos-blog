// tests/test_crypto.cpp
// Unit tests for Ed25519 operations in src/crypto.cpp.
// Uses real OpenSSL — no mocking required.

#include <QtTest>
#include "crypto.h"

class TestCrypto : public QObject {
    Q_OBJECT

private slots:
    void generateKeypair_validHexLengths();
    void generateKeypair_keysAreDifferent();
    void sign_returnsHex128Chars();
    void signVerify_roundtrip();
    void verify_rejectsWrongMessage();
    void verify_rejectsWrongKey();
    void verify_rejectsCorruptedSignature();
    void sign_deterministic();
    void signVerify_emptyMessage();
};

// ── generateKeypair ───────────────────────────────────────────────────────────

void TestCrypto::generateKeypair_validHexLengths()
{
    const Keypair kp = Crypto::generateEd25519Keypair();
    QVERIFY(!kp.pubkeyHex.isEmpty());
    QVERIFY(!kp.privkeyHex.isEmpty());
    // Ed25519: 32-byte public key → 64 hex chars
    QCOMPARE(kp.pubkeyHex.length(), 64);
    // 32-byte private seed → 64 hex chars
    QCOMPARE(kp.privkeyHex.length(), 64);
    // Must be lowercase hex only
    static const QRegularExpression hexRe("^[0-9a-f]+$");
    QVERIFY(hexRe.match(kp.pubkeyHex).hasMatch());
    QVERIFY(hexRe.match(kp.privkeyHex).hasMatch());
}

void TestCrypto::generateKeypair_keysAreDifferent()
{
    const Keypair kp1 = Crypto::generateEd25519Keypair();
    const Keypair kp2 = Crypto::generateEd25519Keypair();
    QVERIFY(kp1.pubkeyHex  != kp2.pubkeyHex);
    QVERIFY(kp1.privkeyHex != kp2.privkeyHex);
}

// ── sign ─────────────────────────────────────────────────────────────────────

void TestCrypto::sign_returnsHex128Chars()
{
    const Keypair kp  = Crypto::generateEd25519Keypair();
    const QString sig = Crypto::sign(kp.privkeyHex, "hello");
    QVERIFY(!sig.isEmpty());
    // Ed25519 signature: 64 bytes → 128 hex chars
    QCOMPARE(sig.length(), 128);
    static const QRegularExpression hexRe("^[0-9a-f]+$");
    QVERIFY(hexRe.match(sig).hasMatch());
}

// ── verify ────────────────────────────────────────────────────────────────────

void TestCrypto::signVerify_roundtrip()
{
    const Keypair    kp  = Crypto::generateEd25519Keypair();
    const QByteArray msg = "Hello, Logos Blog!";
    const QString    sig = Crypto::sign(kp.privkeyHex, msg);
    QVERIFY(!sig.isEmpty());
    QVERIFY(Crypto::verify(kp.pubkeyHex, sig, msg));
}

void TestCrypto::verify_rejectsWrongMessage()
{
    const Keypair kp = Crypto::generateEd25519Keypair();
    const QString sig = Crypto::sign(kp.privkeyHex, "message A");
    QVERIFY(!Crypto::verify(kp.pubkeyHex, sig, "message B"));
}

void TestCrypto::verify_rejectsWrongKey()
{
    const Keypair    kp1 = Crypto::generateEd25519Keypair();
    const Keypair    kp2 = Crypto::generateEd25519Keypair();
    const QByteArray msg = "test message";
    const QString    sig = Crypto::sign(kp1.privkeyHex, msg);
    // Signed with kp1 — should not verify against kp2's public key
    QVERIFY(!Crypto::verify(kp2.pubkeyHex, sig, msg));
}

void TestCrypto::verify_rejectsCorruptedSignature()
{
    const Keypair    kp  = Crypto::generateEd25519Keypair();
    const QByteArray msg = "test message";
    QString sig = Crypto::sign(kp.privkeyHex, msg);
    QVERIFY(!sig.isEmpty());
    // Flip the first hex character
    sig[0] = (sig[0] == QChar('a')) ? QChar('b') : QChar('a');
    QVERIFY(!Crypto::verify(kp.pubkeyHex, sig, msg));
}

// ── determinism ──────────────────────────────────────────────────────────────

void TestCrypto::sign_deterministic()
{
    // Ed25519 (as implemented by OpenSSL EVP) is deterministic: same key+msg
    // always produces the same signature.
    const Keypair    kp  = Crypto::generateEd25519Keypair();
    const QByteArray msg = "determinism test";
    QCOMPARE(Crypto::sign(kp.privkeyHex, msg),
             Crypto::sign(kp.privkeyHex, msg));
}

// ── edge cases ────────────────────────────────────────────────────────────────

void TestCrypto::signVerify_emptyMessage()
{
    const Keypair    kp  = Crypto::generateEd25519Keypair();
    const QByteArray msg = "";
    const QString    sig = Crypto::sign(kp.privkeyHex, msg);
    QVERIFY(!sig.isEmpty());
    QVERIFY(Crypto::verify(kp.pubkeyHex, sig, msg));
}

QTEST_GUILESS_MAIN(TestCrypto)
#include "test_crypto.moc"
