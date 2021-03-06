
/*
 * @copyright
 *
 *  Copyright 2014 Neo Natura
 *
 *  This file is part of ShionCoin.
 *  (https://github.com/neonatura/shioncoin)
 *        
 *  ShionCoin is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version. 
 *
 *  ShionCoin is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with ShionCoin.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  @endcopyright
 */  

#ifndef __TEST__TEST_BLOCK_H__
#define __TEST__TEST_BLOCK_H__




#if 0
class TEST_CTxMemPool : public CTxMemPool
{

  public:
    bool accept(CTxDB& txdb, CTransaction &tx, bool fCheckInputs, bool* pfMissingInputs);
    bool addUnchecked(const uint256& hash, CTransaction &tx);
    bool remove(CTransaction &tx);
    void queryHashes(std::vector<uint256>& vtxid);

};
#endif


class TESTBlock : public CBlock
{
public:
    // header
    static const int CURRENT_VERSION=4;
    static TEST_CTxMemPool mempool; 
    static CBlockIndex *pindexBest;
    static CBlockIndex *pindexGenesisBlock;// = NULL;
    static int64 nTimeBestReceived ;//= 0;

    static int64 nTargetTimespan;
    static int64 nTargetSpacing;

    TESTBlock()
    {
        ifaceIndex = TEST_COIN_IFACE;
        SetNull();
    }
    TESTBlock(const CBlock &block)
    {
        ifaceIndex = TEST_COIN_IFACE;
        SetNull();
        *((CBlock*)this) = block;
    }

    void SetNull()
    {
      nVersion = TESTBlock::CURRENT_VERSION;
      CBlock::SetNull();

    }

    void InvalidChainFound(CBlockIndex* pindexNew);
    unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast);
    bool AcceptBlock();
    bool IsBestChain();
    CScript GetCoinbaseFlags();
    bool AddToBlockIndex();
    bool CheckBlock();
    bool ReadBlock(uint64_t nHeight);
    bool ReadArchBlock(uint256 hash);
    bool IsOrphan();
    bool Truncate();
    bool VerifyCheckpoint(int nHeight);
    uint64_t GetTotalBlocksEstimate();

    int64_t GetBlockWeight();

//  protected: bool SetBestChainInner(CTxDB& txdb, CBlockIndex *pindexNew);

		bool CreateCheckpoint(); 

		int GetAlgo() const;

#ifdef USE_LEVELDB_COINDB
    bool SetBestChain(CTxDB& txdb, CBlockIndex* pindexNew);
    bool ConnectBlock(CTxDB& txdb, CBlockIndex* pindex);
    bool DisconnectBlock(CTxDB& txdb, CBlockIndex* pindex);
#else
    bool SetBestChain(CBlockIndex *pindexNew);
    bool ConnectBlock(CBlockIndex* pindex);
    bool DisconnectBlock(CBlockIndex* pindex);
#endif

};



extern TEST_CTxMemPool TEST_mempool;

extern CBlockIndex* TEST_pindexGenesisBlock;
extern int TEST_nBestHeight;
extern uint256 TEST_hashBestChain;
extern CBlockIndex* TEST_pindexBest;
extern int64 TEST_nTimeBestReceived;
extern uint256 test_hashGenesisBlock;

CBlock* test_CreateNewBlock(const CPubKey& rkey, CBlockIndex *pindexPrev = NULL);

CBlock *test_GenerateBlock(CBlockIndex *pindexPrev = NULL);

bool test_CreateGenesisBlock();

bool test_SetBestChain(CBlock *block);


bool test_ProcessBlock(CNode* pfrom, CBlock* pblock);

bool test_CheckBlock(CBlock *block);

void test_SyncWithWallets(const CTransaction& tx, const CBlock* pblock, bool fUpdate);

int64 test_GetBlockValue(int nHeight, int64 nFees);

bool test_IsOrphanBlock(const uint256& hash); 
void test_AddOrphanBlock(CBlock *block); 
void test_RemoveOrphanBlock(const uint256& hash); 
bool test_GetOrphanPrevHash(const uint256& hash, uint256& retPrevHash); 
CBlock *test_GetOrphanBlock(const uint256& hash); 
uint256 test_GetOrphanRoot(uint256 hash); 



#endif /* ndef __TEST__TEST_BLOCK_H__ */
