#ifndef BITCOIN_CORE_H
#define BITCOIN_CORE_H

#include "uint256.h"
#include "serialize.h"
#include "util.h"
#include "script.h"

#include <stdio.h>

class CTransaction;

/** Outpoint */
class COutPoint
{
public:
    uint256 hash;
    unsigned int n;

    COutPoint() { SetNull(); }
    COutPoint(uint256 hashIn, unsigned int nIn) { hash = hashIn; n = nIn; }

    IMPLEMENT_SERIALIZE
    (
        READWRITE(hash);
        READWRITE(n);
    )

    void SetNull() { hash = 0; n = (unsigned int)-1; }
    bool IsNull() const { return (hash == 0 && n == (unsigned int)-1); }
};

/** Inpoint */
class CInPoint
{
public:
    CTransaction* ptx;
    unsigned int n;

    CInPoint() { SetNull(); }
    CInPoint(CTransaction* ptxIn, unsigned int nIn) { ptx = ptxIn; n = nIn; }

    void SetNull() { ptx = NULL; n = (unsigned int)-1; }
    bool IsNull() const { return (ptx == NULL && n == (unsigned int)-1); }
};

/** Transaction input */
class CTxIn
{
public:
    COutPoint prevout;
    CScript scriptSig;
    unsigned int nSequence;

    CTxIn() { nSequence = std::numeric_limits<unsigned int>::max(); }

    CTxIn(COutPoint prevoutIn, CScript scriptSigIn, unsigned int nSequenceIn)
    {
        prevout = prevoutIn;
        scriptSig = scriptSigIn;
        nSequence = nSequenceIn;
    }

    IMPLEMENT_SERIALIZE
    (
        READWRITE(prevout);
        READWRITE(scriptSig);
        READWRITE(nSequence);
    )
};

/** Transaction output */
class CTxOut
{
public:
    int64_t nValue;
    CScript scriptPubKey;

    CTxOut() { SetNull(); }
    CTxOut(int64_t nValueIn, CScript scriptPubKeyIn)
    {
        nValue = nValueIn;
        scriptPubKey = scriptPubKeyIn;
    }

    IMPLEMENT_SERIALIZE
    (
        READWRITE(nValue);
        READWRITE(scriptPubKey);
    )

    void SetNull()
    {
        nValue = -1;
        scriptPubKey.clear();
    }
};

/** MicroBlock (your addition) */
class CMicroBlock
{
public:
    uint256 hashPrevMicroBlock;
    CTransaction tx;
    uint32_t nTime;
    uint32_t nMicroHeight;
    uint32_t nBits;
    uint32_t nNonce;

    IMPLEMENT_SERIALIZE
    (
        READWRITE(hashPrevMicroBlock);
        READWRITE(tx);
        READWRITE(nTime);
        READWRITE(nMicroHeight);
        READWRITE(nBits);
        READWRITE(nNonce);
    )

    uint256 GetHash() const;
    bool IsValid() const;
};

#endif
