
/*
 * @copyright
 *
 *  Copyright 2018 Neo Natura
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

#include "shcoind.h"
#include "block.h"
#include "db.h"
#include <vector>
#include "spring.h"
#include "versionbits.h"
#include "wit_merkle.h"
#include "txmempool.h"
#include "coin.h"
#include "wallet.h"
#include "algobits.h"

using namespace std;

/** Flags for nSequence and nLockTime locks */
/** Interpret sequence numbers as relative lock-time constraints. */
static const unsigned int LOCKTIME_VERIFY_SEQUENCE = (1 << 0);
/** Use GetMedianTimePast() instead of nTime for end point timestamp. */
static const unsigned int LOCKTIME_MEDIAN_TIME_PAST = (1 << 1);
static const unsigned int STANDARD_LOCKTIME_VERIFY_FLAGS = 
		LOCKTIME_VERIFY_SEQUENCE |
		LOCKTIME_MEDIAN_TIME_PAST;


bool CheckBlockHeader(CBlockHeader *pblock)
{
	CBigNum bnTarget;

	bnTarget.SetCompact(pblock->nBits);

  /* check proof of work exceeds block expectations. */
  if (pblock->GetPoWHash() > bnTarget.getuint256())
		return (false);

	return (true);
}

bool ContextualCheckBlockHeader(CIface *iface, const CBlockHeader& block, CBlockIndex *pindexPrev)
{
	const int ifaceIndex = GetCoinIndex(iface);
	int nHeight = (pindexPrev ? (pindexPrev->nHeight+1) : 0);

	if (ifaceIndex != EMC2_COIN_IFACE &&
			ifaceIndex != USDE_COIN_IFACE) {
		/* verify block version. */
		if (iface->BIP34Height != -1 && 
				(uint32_t)nHeight >= iface->BIP34Height) {
			/* check for obsolete blocks. */
			if ((uint32_t)block.nVersion < 2)
				return (error(SHERR_INVAL, "(%s) AcceptBlock: rejecting obsolete block (ver: %u) (hash: %s) [BIP34].", iface->name, (unsigned int)block.nVersion, block.GetHash().GetHex().c_str()));
		}
		if (iface->BIP66Height != -1 && 
				(uint32_t)block.nVersion < 3 &&
				(uint32_t)nHeight >= iface->BIP66Height) {
			return (error(SHERR_INVAL, "(%s) AcceptBlock: rejecting obsolete block (ver: %u) (hash: %s) [BIP66].", iface->name, (unsigned int)block.nVersion, block.GetHash().GetHex().c_str()));
		}
		if (iface->BIP65Height != -1 && 
				(uint32_t)block.nVersion < 4 &&
				(uint32_t)nHeight >= iface->BIP65Height) {
			return (error(SHERR_INVAL, "(%s) AcceptBlock: rejecting obsolete block (ver: %u) (hash: %s) [BIP65].", iface->name, (unsigned int)block.nVersion, block.GetHash().GetHex().c_str()));
		}
	}
	if ((uint32_t)block.nVersion < VERSIONBITS_TOP_BITS &&
			IsWitnessEnabled(iface, pindexPrev))
		return (error(ERR_INVAL, "(%s) ContextualCheckBlockHeader: invalid block version for witness enabled chain.", iface->name));

	return (true);
}

bool CheckBlock(CBlock *block)
{
  CIface *iface = GetCoinByIndex(block->ifaceIndex);
	uint32_t nSigOps;

	if (!CheckBlockHeader(block)) {
		if (block->originPeer)
			block->originPeer->PushReject("block", block->GetHash(), REJECT_INVALID, "bad-diffbits");
		return (false);
	}

	/* check merkleroot. */
	if (block->hashMerkleRoot != block->BuildMerkleTree()) {
		if (block->originPeer)
			block->originPeer->PushReject("block", block->GetHash(), REJECT_INVALID, "bad-txnmrklroot");
		return error(SHERR_INVAL, "(%s) CheckBlock: hashMerkleRoot mismatch", iface->name);
	}

	/* verify coinbase integrity */
	if (block->vtx.empty() || !block->vtx[0].IsCoinBase()) {
		if (block->originPeer)
			block->originPeer->PushReject("block", block->GetHash(), REJECT_INVALID, "bad-cb-missing"); 
		return error(SHERR_INVAL, "(%s) CheckBlock: first tx is not coinbase", iface->name);
	}
	for (unsigned int i = 1; i < block->vtx.size(); i++) {
		if (block->vtx[i].IsCoinBase()) {
			if (block->originPeer)
				block->originPeer->PushReject("block", block->GetHash(), REJECT_INVALID, "bad-cb-multiple");
			return error(SHERR_INVAL, "(%s) CheckBlock: more than one coinbase", iface->name);
		}
	}

	/* check transactions. */
	BOOST_FOREACH(CTransaction& tx, block->vtx) {
		if (!tx.CheckTransaction(block->ifaceIndex))
			return error(SHERR_INVAL, "(%s) CheckBlock: CheckTransaction failed", iface->name);
	}

	nSigOps = 0;
	BOOST_FOREACH(const CTransaction& tx, block->vtx)
		nSigOps += tx.GetLegacySigOpCount();
	if (nSigOps > MAX_BLOCK_SIGOPS(iface)) {
		if (block->originPeer)
			block->originPeer->PushReject("block", block->GetHash(), REJECT_INVALID, "bad-blk-sigops");
		return (block->trust(-100, "(%s) CheckBlock: out-of-bounds SigOpCount", iface->name));
	}

	/* coin service specific checks (non-contextual). */
	return (block->CheckBlock());
}

bool ContextualCheckBlock(CBlock *pblock, CBlockIndex *pindexPrev)
{
  CIface *iface = GetCoinByIndex(pblock->ifaceIndex);
	int nHeight = (pindexPrev ? (pindexPrev->nHeight+1) : 0);
	VersionBitsCache *cache;
	int64_t nWeight;

	if (!pblock)
		return (false);

	unsigned int flags = GetBlockScriptFlags(iface, pindexPrev);
	int64_t nLockTimeCutoff = (flags & SCRIPT_VERIFY_CHECKSEQUENCEVERIFY) ?
		pindexPrev->GetMedianTimePast() : pblock->GetBlockTime();

	/* check that all transactions are finalized. */ 
  BOOST_FOREACH(const CTransaction& tx, pblock->vtx) {
		if (!tx.IsFinal(pblock->ifaceIndex, nHeight, nLockTimeCutoff))
			return (pblock->trust(-10, "(core) AcceptBlock: block contains a non-final transaction at height %u.", nHeight));
  }

	/* verify block's minimum difficulty. */
	if (pblock->nBits != pblock->GetNextWorkRequired(pindexPrev))
    return (pblock->trust(-100, "(%s) ContextualCheckBlock: invalid difficulty (%x) specified (next work required is %x) for block height %d [prev '%s']\n", iface->name, pblock->nBits, pblock->GetNextWorkRequired(pindexPrev), nHeight, pindexPrev->GetBlockHash().GetHex().c_str()));

  /* check that the block matches the known block hash for last checkpoint. */
  if (!pblock->VerifyCheckpoint(nHeight)) {
		if (pblock->originPeer)
			pblock->originPeer->PushReject("block", pblock->GetHash(), REJECT_INVALID, "bad-fork-prior-to-checkpoint");
    return (pblock->trust(-100, "(%s) ContextualCheckBlock: rejected by checkpoint lockin at height %u", iface->name, nHeight));
	}

	if (iface->BIP34Height != -1 && nHeight >= iface->BIP34Height) {
		/* verify height inclusion. */
		CScript expect = CScript() << nHeight;
		if (pblock->vtx[0].vin[0].scriptSig.size() < expect.size() ||
				!std::equal(expect.begin(), expect.end(), pblock->vtx[0].vin[0].scriptSig.begin())) {
			if (pblock->originPeer)
				pblock->originPeer->PushReject("block", pblock->GetHash(), REJECT_INVALID, "block height mismatch in coinbase");
			return error(SHERR_INVAL, "(%s) AcceptBlock: submit block \"%s\" has invalid commit height (next block height is %u): %s.", iface->name, pblock->GetHash().GetHex().c_str(), nHeight, pblock->vtx[0].vin[0].scriptSig.ToString().c_str());
		}
	}

	if (!core_CheckBlockWitness(iface, pblock, pindexPrev)) {
		return (error(ERR_INVAL, "(%s CheckBlock: invalid witness integrity.", iface->name));
	}

	nWeight = pblock->GetBlockWeight();
	if (nWeight > MAX_BLOCK_WEIGHT(iface)) {
		if (pblock->originPeer)
			pblock->originPeer->PushReject("block", pblock->GetHash(), REJECT_INVALID, "bad-blk-weight");
		return (pblock->trust(-80, "(%s) CheckBlock: block weight (%d) > max (%d) [hash %s]", iface->name, nWeight, MAX_BLOCK_WEIGHT(iface), pblock->GetHash().GetHex().c_str()));
	}

	/* check PoW algorythm restrictions. */
	int alg = pblock->GetAlgo();
	if (alg != ALGO_SCRYPT) {
		CWallet *wallet = GetWallet(iface);
		if (!wallet->IsAlgoSupported(alg, pindexPrev, pblock->hColor)) {
			return (error(ERR_INVAL, "(%s) CheckBlock: invalid PoW algorthm %d (%s).", iface->name, alg, GetAlgoName(alg)));
		}
	}

	return (true);
}

CBlockIndex *CreateBlockIndex(CIface *iface, CBlockHeader& block)
{
	CWallet *wallet = GetWallet(iface);
	const int ifaceIndex = GetCoinIndex(iface);
  blkidx_t *blockIndex = GetBlockTable(ifaceIndex);
  uint256 hash = block.GetHash();
  CBlockIndex *pindexNew;

  /* check for duplicate. */
  if (blockIndex->count(hash) != 0) { 
		return ( (*blockIndex)[hash] );
	}

  /* create new index */
  pindexNew = new CBlockIndex(block);
  if (!pindexNew)
    return (NULL);

  map<uint256, CBlockIndex*>::iterator mi = blockIndex->insert(make_pair(hash, pindexNew)).first;
  pindexNew->phashBlock = &((*mi).first);

  map<uint256, CBlockIndex*>::iterator miPrev = blockIndex->find(block.hashPrevBlock);
  if (miPrev != blockIndex->end())
  {
    pindexNew->pprev = (*miPrev).second;
    pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
    pindexNew->BuildSkip();
  }

	bool fAlgo = false;
	if (ifaceIndex == TEST_COIN_IFACE ||
			ifaceIndex == TESTNET_COIN_IFACE ||
			ifaceIndex == SHC_COIN_IFACE ||
			ifaceIndex == COLOR_COIN_IFACE)
		fAlgo = true;
  pindexNew->bnChainWork = (pindexNew->pprev ? pindexNew->pprev->bnChainWork : 0) + pindexNew->GetBlockWork(fAlgo);

#if 0
  if (IsWitnessEnabled(iface, pindexNew->pprev)) {
    pindexNew->nStatus |= BLOCK_OPT_WITNESS;
  }
#endif

#if 0
  if (pindexNew->bnChainWork > bnBestChainWork) {
    bool ret = SetBestChain(pindexNew);
    if (!ret)
      return (false);
  } else {
    WriteArchBlock();
  }
#endif

	pindexNew->RaiseValidity(BLOCK_VALID_TREE);

	if (!wallet->pindexBestHeader ||
			wallet->pindexBestHeader->bnChainWork < pindexNew->bnChainWork) {
		wallet->pindexBestHeader = pindexNew;
	}

  return (pindexNew);
}

#if 0
uint256 GetGenesisBlockHash(CIface *iface, CBlockIndex *pindex)
{
	uint256 blank = 0;

	if (pindex) {
		while (pindex && pindex->pprev)
			pindex = pindex->pprev;
		if (pindex && !pindex->pprev)
			return (pindex->GetBlockHash());
	}

	if (!iface)
		return (blank);

	uint256 hash;
	CBlock *block = GetBlockByHeight(iface, 0);
	if (!block)
		return (blank);
	hash = block->GetHash();
	delete block;
	return (hash);
}
#endif

bool core_AcceptBlockHeader(CIface *iface, CBlockHeader& block, CBlockIndex **pindex_p)
{
	const int ifaceIndex = GetCoinIndex(iface);
	CBlockIndex *pindex;
	CBlockIndex *pindexPrev;
	uint256 hash = block.GetHash();

	pindex = GetBlockIndexByHash(ifaceIndex, hash); 
	pindexPrev = GetBlockIndexByHash(ifaceIndex, block.hashPrevBlock);

	block.ifaceIndex = ifaceIndex;
	if (block.hashPrevBlock != 0) {
		if (pindex) { /* already procesed */
			if (pindex_p)
				*pindex_p = pindex;
			if (pindex->nStatus & BLOCK_FAILED_MASK)
				return (error(ERR_INVAL, "(%s) AcceptBlockHeader: block \"%s\" is marked as invalid.", iface->name, hash.GetHex().c_str()));
			return (true);
		}
		if (!pindexPrev) {
			/* unknown previous block hash */
			return (error(ERR_NOENT, "(%s) core_AcceptBlockHeader: unknown previous block hash \"%s\".", iface->name, block.hashPrevBlock.GetHex().c_str()));
		}
		if (pindexPrev->nStatus & BLOCK_FAILED_MASK) {
			return (error(ERR_INVAL, "(%s) AcceptBlockHeader: previous block of \"%s\" is marked as invalid.", iface->name, hash.GetHex().c_str()));
		}

		if (!CheckBlockHeader(&block)) {
			return (error(ERR_INVAL, "(%s) AcceptBlockHeader: unable to verify block header (%s)", iface->name, hash.GetHex().c_str()));
		}
		if (!ContextualCheckBlockHeader(iface, block, pindexPrev)) {
			return (error(ERR_INVAL, "(%s) AcceptBlockHeader: unable to verify block header (%s) [contextual]", iface->name, hash.GetHex().c_str()));
		}
	}

	if (!pindex) {
		pindex = CreateBlockIndex(iface, block);
	}
	if (pindex_p) {
		*pindex_p = pindex;
	}

//	Debug("(%s) AcceptBlockHeader: successfully processed block \"%s\" header (height %d).", iface->name, pindex->GetBlockHash().GetHex().c_str(), pindex->nHeight);

	return (true);
}

bool ProcessNewBlockHeaders(CIface *iface, std::vector<CBlockHeader>& headers, CBlockIndex** ppindex)
{

	for (int i = 0; i < headers.size(); i++) {
		CBlockHeader& header = headers[i];
		if (!core_AcceptBlockHeader(iface, header, ppindex)) {
			return false;
		}
	}

	return (true);
}

#if 0
bool RelayBlock(CBlock *pblock)
{
  CIface *iface = GetCoinByIndex(pblock->ifaceIndex);

	int nBlockEstimate = pblock->GetTotalBlocksEstimate();
	if (GetBestHeight(iface) > nBlockEstimate)
		return (false);

	{
		LOCK(cs_vNodes);

		const uint256& hash = pblock->GetHash();
		NodeList &vNodes = GetNodeList(pblock->ifaceIndex); 
		BOOST_FOREACH(CNode* pnode, vNodes) {
			pnode->PushInventory(CInv(pblock->ifaceIndex, MSG_BLOCK, hash));
		}
	}

}
#endif

bool core_AcceptBlock(CBlock *pblock, CBlockIndex *pindexPrev)
{
  int ifaceIndex = pblock->ifaceIndex;
  CIface *iface = GetCoinByIndex(ifaceIndex);
	CWallet *wallet = GetWallet(iface);
  uint256 hash = pblock->GetHash();
	CBlockIndex *pindex;
  shtime_t ts;
	int mode;
  bool ret;

  if (!pblock || !pindexPrev) {
    return error(SHERR_INVAL, "(%s) core_AcceptBlock: invalid parameter.", iface->name);
  }

	if (!pblock->CheckTransactionInputs(ifaceIndex))
		return (error(SHERR_INVAL, "(%s) core_AcceptBlock: invalid inputs specified [hash %s].", iface->name, hash.GetHex().c_str()));

	if (!core_AcceptBlockHeader(iface, *pblock, &pindex)) {
		return (error(SHERR_INVAL, "(%s) core_AcceptBlock: invalid block header [hash %s].", iface->name, hash.GetHex().c_str()));
	}

	if (pindex->nStatus & BLOCK_HAVE_DATA) {
		Debug("(%s) AcceptBlock: skipping already processed block \"%s\".", iface->name, hash.GetHex().c_str(), pblock->originPeer ? pblock->originPeer->addr.ToString().c_str() : "<local>");
		return (true);
	}
#if 0
	CBlockIndex *pindexBest = GetBestBlockIndex(pblock->ifaceIndex);
	if (pindexBest && pindex->nHeight <= pindexBest->nHeight &&
			HasBlockHash(iface, hash)) {
		/* already processed. */
		return (true);
	}
#endif

  unsigned int nHeight = pindexPrev->nHeight+1;

	if (!CheckBlock(pblock)) {
		/* record that block could not be validated. */
		iface->net_invalid = time(NULL);
		pindex->nStatus |= BLOCK_FAILED_VALID;
		return (error(ERR_INVAL, "(%s) core_AcceptBlock: error verifying block."));
	}
	if (!ContextualCheckBlock(pblock, pindexPrev)) {
		/* record that block could not be validated. */
		iface->net_invalid = time(NULL);
		pindex->nStatus |= BLOCK_FAILED_VALID;
		return (error(ERR_INVAL, "(%s) core_AcceptBlock: error verifying block \"%s\" with height %d [contextual].", iface->name, hash.GetHex().c_str(), nHeight));
	}

#if 0
  if (GetBestBlockChain(iface) == hash) {
		/* inventory relay */
		RelayBlock(pblock);
	}
#endif

	/* record that whole valid block was received. */
	pindex->nStatus |= BLOCK_HAVE_DATA;
	if (IsWitnessEnabled(iface, pindex->pprev)) {
		pindex->nStatus |= BLOCK_OPT_WITNESS;
	}
	pindex->RaiseValidity(BLOCK_VALID_TRANSACTIONS);

	/* ensure chain work is higher than previous block. */
  if (pindex->bnChainWork <= wallet->bnBestChainWork) {
		/* treated as success. block work does not exceed last work. */
    if (pblock->WriteArchBlock()) {
			STAT_BLOCK_ACCEPTS(iface)++;
			pindex->nStatus |= BLOCK_HAVE_UNDO;
		}
		return (true);
	}
	/* write to disk */
	if (!pblock->SetBestChain(pindex))
		return (false);

	/* relay block to the rest of the nodes. */
	if (!IsInitialBlockDownload(ifaceIndex)) {
		LOCK(cs_vNodes);

		int nTot = 0;
		NodeList &vNodes = GetNodeList(pblock->ifaceIndex); 
		BOOST_FOREACH(CNode* pnode, vNodes) {
			if (pnode == pblock->originPeer)
				continue; /* skip peer that sent us block. */

			pnode->PushBlockHash(hash);
			nTot++;
		}
		if (nTot) Debug("(%s) AcceptBlock: informed %d nodes of block \"%s\".", iface->name, nTot, hash.GetHex().c_str()); 
	}

  if (ifaceIndex == TEST_COIN_IFACE ||
			ifaceIndex == TESTNET_COIN_IFACE ||
      ifaceIndex == SHC_COIN_IFACE) {
		/* initiate notary tx, if needed. */
		int mode;
		const CTransaction& tx = pblock->vtx[0];
		if ((tx.GetFlags() & CTransaction::TXF_MATRIX) &&
				GetExtOutputMode(tx, OP_MATRIX, mode) &&
				mode == OP_EXT_VALIDATE) {
			RelayValidateMatrixNotaryTx(iface, tx);
		}
  }

  STAT_BLOCK_ACCEPTS(iface)++;
	iface->net_valid = time(NULL);
	if (pblock->vtx.size() != 0) {
		BOOST_FOREACH(const CTxOut& txout, pblock->vtx[0].vout)
			STAT_TX_MINT(iface) += txout.nValue;
	}

  return true;
}


/**
 * The scrypt legacy method of accepting a new block onto the block-chain.
 */
bool legacy_AcceptBlock(CBlock *pblock, CBlockIndex *pindexPrev)
{
	int ifaceIndex = pblock->ifaceIndex;
	CIface *iface = GetCoinByIndex(ifaceIndex);
	uint256 hash = pblock->GetHash();
	shtime_t ts;
	int mode;
	bool ret;

	if (!pblock || !pindexPrev) {
		return error(SHERR_INVAL, "(core) AcceptBlock: invalid parameter.");
	}

	if (GetBlockIndexByHash(ifaceIndex, hash)) {
		return error(SHERR_INVAL, "(core) AcceptBlock: block already in chain.");
	}
#if 0
	if (HasBlockHash(iface, hash)) {
		return error(SHERR_INVAL, "(core) AcceptBlock: block already in chain.");
	}
#endif

	unsigned int nHeight = pindexPrev->nHeight+1;


	/* check proof of work */
	unsigned int nBits = pblock->GetNextWorkRequired(pindexPrev);
	if (pblock->nBits != nBits) {
		return (pblock->trust(-100, "(core) AcceptBlock: invalid difficulty (%x) specified (next work required is %x) for block height %d [prev '%s']\n", pblock->nBits, nBits, nHeight, pindexPrev->GetBlockHash().GetHex().c_str()));
	}

	BOOST_FOREACH(const CTransaction& tx, pblock->vtx) {
#if 0 /* not standard */
		if (!tx.IsCoinBase()) { // Check that all inputs exist
			BOOST_FOREACH(const CTxIn& txin, tx.vin) {
				if (txin.prevout.IsNull())
					return error(SHERR_INVAL, "AcceptBlock(): prevout is null");
				if (!VerifyTxHash(iface, txin.prevout.hash))
					return error(SHERR_INVAL, "AcceptBlock(): unknown prevout hash '%s'", txin.prevout.hash.GetHex().c_str());
			}
		}
#endif

		/* check that all transactions are finalized. */ 
		unsigned int flags = GetBlockScriptFlags(iface, pindexPrev);
		if (flags & SCRIPT_VERIFY_CHECKSEQUENCEVERIFY) {
			if (!tx.IsFinal(ifaceIndex, nHeight, pindexPrev->GetMedianTimePast()))
				return (pblock->trust(-10, "(core) AcceptBlock: block contains a non-final transaction at height %u [bip113].", nHeight));
		} else {
			if (!tx.IsFinal(ifaceIndex, nHeight, pblock->GetBlockTime()))
				return (pblock->trust(-10, "(core) AcceptBlock: block contains a non-final transaction at height %u [lock].", nHeight));
		}
		if (tx.GetVersion() >= 2) {
			/* tx v2 lock/sequence test */
			if (!CheckSequenceLocks(iface, tx, STANDARD_LOCKTIME_VERIFY_FLAGS))
				return (pblock->trust(-10, "(core) AcceptBlock: block contains a non-final transaction at height %u [seq].", nHeight));
		}
	}

	/* check that the block matches the known block hash for last checkpoint. */
	if (!pblock->VerifyCheckpoint(nHeight)) {
		return (pblock->trust(-100, "(core) AcceptBlock: rejected by checkpoint lockin at height %u", nHeight));
	}

	ret = pblock->AddToBlockIndex();
	if (!ret) {
		pblock->print();
		return error(SHERR_IO, "AcceptBlock: AddToBlockIndex failed");
	}

	CBlockIndex *pindex = GetBlockIndexByHash(ifaceIndex, hash);
	if (pindex)
		pindex->nStatus |= BLOCK_HAVE_DATA;

	/* inventory relay */
	int nBlockEstimate = pblock->GetTotalBlocksEstimate();
//	if (GetBestBlockChain(iface) == hash)
	{
		//    timing_init("AcceptBlock:PushInventory", &ts);
		NodeList &vNodes = GetNodeList(ifaceIndex);
		{
			LOCK(cs_vNodes);
			BOOST_FOREACH(CNode* pnode, vNodes) {
				if (GetBestHeight(iface) > (pnode->nStartingHeight != -1 ? pnode->nStartingHeight - 2000 : nBlockEstimate)) {
					pnode->PushInventory(CInv(ifaceIndex, MSG_BLOCK, hash));
				}
			}
		}
		//    timing_term(ifaceIndex, "AcceptBlock:PushInventory", &ts);
	}

	if (ifaceIndex == TEST_COIN_IFACE ||
			ifaceIndex == TESTNET_COIN_IFACE ||
			ifaceIndex == SHC_COIN_IFACE) {
		/* handle exec checkpoints */
		BOOST_FOREACH(CTransaction& tx, pblock->vtx) {
			if (tx.isFlag(CTransaction::TXF_EXEC) &&
					IsExecTx(tx, mode) && mode == OP_EXT_UPDATE) {
				ProcessExecTx(iface, pblock->originPeer, tx, nHeight);
			}
		}

		BOOST_FOREACH(CTransaction& tx, pblock->vtx) {
#if 0 
			if (tx.isFlag(CTransaction::TXF_MATRIX)) {
				/* handled in coinbase */
			} 
#endif

			if (tx.isFlag(CTransaction::TXF_ALIAS)) {
				bool fRet = CommitAliasTx(iface, tx, nHeight);
				if (!fRet) {
					error(SHERR_INVAL, "CommitAliasTx failure");
				}
			} 

			if (tx.isFlag(CTransaction::TXF_ASSET)) {
				bool fRet = ProcessAssetTx(iface, tx, nHeight);
				if (!fRet) {
					error(SHERR_INVAL, "ProcessAssetTx failure");
				}
			} else if (tx.isFlag(CTransaction::TXF_CERTIFICATE)) {
				InsertCertTable(iface, tx, nHeight);
			} else if (tx.isFlag(CTransaction::TXF_CONTEXT)) {
				int err = CommitContextTx(iface, tx, nHeight);
				if (err) {
					error(err, "CommitContextTx failure");
				}
			} else if (tx.isFlag(CTransaction::TXF_CHANNEL)) {
				/* not implemented. */
			} else if (tx.isFlag(CTransaction::TXF_IDENT)) {
				InsertIdentTable(iface, tx);
			} else if (tx.isFlag(CTransaction::TXF_LICENSE)) {
				bool fRet = CommitLicenseTx(iface, tx, nHeight);
				if (!fRet) {
					error(SHERR_INVAL, "CommitLicenseTx failure");
				}
			} 

			/* non-exlusive */
			if (tx.isFlag(CTransaction::TXF_OFFER)) {
				int err = CommitOfferTx(iface, tx, nHeight);
				if (err)
					error(err, "CommitOfferTx");
			}

			/* non-exclusive */
			if (tx.isFlag(CTransaction::TXF_EXEC) &&
					IsExecTx(tx, mode) && mode != OP_EXT_UPDATE) {
				ProcessExecTx(iface, pblock->originPeer, tx, nHeight);
			}

			/* non-exclusive */
			if (tx.isFlag(CTransaction::TXF_ALTCHAIN)) {
				if (IsAltChainTx(tx)) {
					CommitAltChainTx(iface, tx, pblock->originPeer);
				}
			}

			/* check for matrix validation notary tx's. */
			ProcessValidateMatrixNotaryTx(iface, tx);
		}

	}

	STAT_BLOCK_ACCEPTS(iface)++;
	return true;
}


