#include "core.h"
#include "hash.h"
#include "util.h"

uint256 CMicroBlock::GetHash() const {
    CHashWriter ss(SER_GETHASH, 0);
    ss << hashPrevMicroBlock;
    ss << tx;
    ss << nTime;
    ss << nMicroHeight;
    ss << nBits;
    ss << nNonce;
    return ss.GetHash();
}

bool CMicroBlock::IsValid() const {
    return GetHash() < UintToArith256(CBigNum().SetCompact(nBits)).GetUint256();
}

uint256 CBlock::BuildMicroMerkleRoot() const {
    std::vector<uint256> hashes;
    for (const CMicroBlock& mb : vMicroBlocks)
        hashes.push_back(mb.GetHash());

    return ComputeMerkleRoot(hashes);
}
