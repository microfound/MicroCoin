// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assert.h"

#include "chainparams.h"
#include "main.h"
#include "util.h"

#include <boost/assign/list_of.hpp>

using namespace boost::assign;

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

#include "chainparamsseeds.h"

//
// Main network
//

// Convert the pnSeeds6 array into usable address objects.
static void convertSeed6(std::vector<CAddress> &vSeedsOut, const SeedSpec6 *data, unsigned int count)
{
    // It'll only connect to one or two seed nodes because once it connects,
    // it'll get a pile of addresses with newer timestamps.
    // Seed nodes are given a random 'last seen time' of between one and two
    // weeks ago.
    const int64_t nOneWeek = 7*24*60*60;
    for (unsigned int i = 0; i < count; i++)
    {
        struct in6_addr ip;
        memcpy(&ip, data[i].addr, sizeof(ip));
        CAddress addr(CService(ip, data[i].port));
        addr.nTime = GetTime() - GetRand(nOneWeek) - nOneWeek;
        vSeedsOut.push_back(addr);
    }
}

class CMainParams : public CChainParams {
public:
 CMainParams() {
    pchMessageStart[0] = 0xA4;
    pchMessageStart[1] = 0xF9;
    pchMessageStart[2] = 0xC2;
    pchMessageStart[3] = 0x7D;

    vAlertPubKey = ParseHex("0403b1f9c7a2d4e8f1c9b2a7d3e4f8c1b9a2d7e3f4c8b1a9d2e7f3c4b8a1d9e2f3");

    nDefaultPort = 29841;
    nRPCPort     = 29842;
     
    bnProofOfWorkLimit = CBigNum(~uint256(0) >> 20);

    const char* pszTimestamp = "chain launched.";

    std::vector<CTxIn> vin(1);
    vin[0].scriptSig = CScript() << 0 << CBigNum(42)
        << std::vector<unsigned char>(
            (const unsigned char*)pszTimestamp,
            (const unsigned char*)pszTimestamp + strlen(pszTimestamp));

    std::vector<CTxOut> vout(1);
    vout[0].SetEmpty();

    CTransaction txNew(1, 1723704000, vin, vout, 0);
    genesis.vtx.push_back(txNew);
    genesis.hashPrevBlock = 0;
    genesis.hashMerkleRoot = genesis.BuildMerkleTree();
    genesis.nVersion = 1;
    genesis.nTime    = 1723704000;
    genesis.nBits    = bnProofOfWorkLimit.GetCompact();
    genesis.nNonce   = 512884;

    hashGenesisBlock = genesis.GetHash();

    vSeeds.clear();
    vFixedSeeds.clear();

    vSeeds.push_back(CDNSSeedData("microcoin.rf.gd", "microcoin.rf.gd/dnsseed"));

    base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 37);
    base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 19);
    base58Prefixes[SECRET_KEY]     = std::vector<unsigned char>(1, 203);

    base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E)
        .convert_to_container<std::vector<unsigned char> >();
    base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4)
        .convert_to_container<std::vector<unsigned char> >();

    nLastPOWBlock = 2147483647;
}

    virtual const CBlock& GenesisBlock() const { return genesis; }
    virtual Network NetworkID() const { return CChainParams::MAIN; }

    virtual const vector<CAddress>& FixedSeeds() const {
        return vFixedSeeds;
    }
protected:
    CBlock genesis;
    vector<CAddress> vFixedSeeds;
};
static CMainParams mainParams;


//
// Testnet
//

class CTestNetParams : public CMainParams {
public:
    CTestNetParams() {
       pchMessageStart[0] = 0x52;
       pchMessageStart[1] = 0xC8;
       pchMessageStart[2] = 0xF1;
       pchMessageStart[3] = 0x3A;

       bnProofOfWorkLimit = CBigNum(~uint256(0) >> 8);

       vAlertPubKey = ParseHex("0409c3f1e7a2d8c4b1f9e3d7c2a8b4f1d9e7c3a2b1f4d8e3c7a9b2f1e4c3d7a8");

       nDefaultPort = 19841;
       nRPCPort     = 19842;

       strDataDir = "testnet";

       genesis.nBits  = bnProofOfWorkLimit.GetCompact();
       genesis.nTime  = 1723704100;
       genesis.nNonce = 99214;

       hashGenesisBlock = genesis.GetHash();

       vSeeds.clear();
       vFixedSeeds.clear();

       vSeeds.push_back(CDNSSeedData("microcoin.rf.gd", "microcoin.rf.gd/dnsseedtest"));

       base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 111);
       base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 196);
       base58Prefixes[SECRET_KEY]     = std::vector<unsigned char>(1, 239);
    }
    virtual Network NetworkID() const { return CChainParams::TESTNET; }
};
static CTestNetParams testNetParams;




static CChainParams *pCurrentParams = &mainParams;

const CChainParams &Params() {
    return *pCurrentParams;
}

void SelectParams(CChainParams::Network network) {
    switch (network) {
        case CChainParams::MAIN:
            pCurrentParams = &mainParams;
            break;
        case CChainParams::TESTNET:
            pCurrentParams = &testNetParams;
            break;

        default:
            assert(false && "Unimplemented network");
            return;
    }
}

bool SelectParamsFromCommandLine() {

    bool fTestNet = GetBoolArg("-testnet", false);



    if (fTestNet) {
        SelectParams(CChainParams::TESTNET);
    } else {
        SelectParams(CChainParams::MAIN);
    }
    return true;
}
