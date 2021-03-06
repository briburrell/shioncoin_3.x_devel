
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

#include "shcoind.h"
#include "net.h"
#include "init.h"
#include "strlcpy.h"
#include "ui_interface.h"
#include "algobits.h"

#ifdef WIN32
#include <string.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef fcntl
#undef fcntl
#endif

#include <boost/array.hpp>
#include <share.h>
#include "walletdb.h"
#include "usde/usde_pool.h"
#include "usde/usde_block.h"
#include "usde/usde_wallet.h"
#include "usde/usde_txidx.h"
#include "chain.h"
#include "txsignature.h"

using namespace std;
using namespace boost;

USDEWallet *usdeWallet;
CScript USDE_COINBASE_FLAGS;


bool usde_LoadWallet(void)
{
  CIface *iface = GetCoinByIndex(USDE_COIN_IFACE);
  std::ostringstream strErrors;

  const char* pszP2SH = "/P2SH/";
  USDE_COINBASE_FLAGS << std::vector<unsigned char>(pszP2SH, pszP2SH+strlen(pszP2SH));

#if 0
  if (!bitdb.Open(GetDataDir()))
  {
    fprintf(stderr, "error: unable to open data directory.\n");
    return (false);
  }

  if (!LoadBlockIndex(iface)) {
    fprintf(stderr, "error: unable to open load block index.\n");
    return (false);
  }
#endif

  bool fFirstRun = true;
  usdeWallet->LoadWallet(fFirstRun);

  if (fFirstRun)
  {
		string strAccount("");
		CPubKey newDefaultKey = GetAccountPubKey(usdeWallet, strAccount, true);
		//CPubKey newDefaultKey = usdeWallet->GenerateNewKey();
		usdeWallet->SetDefaultKey(newDefaultKey);
		usdeWallet->SetAddressBookName(usdeWallet->vchDefaultKey.GetID(), "");
  }

  //RegisterWallet(usdeWallet);

#if 0
  CBlockIndex *pindexRescan = GetBestBlockIndex(USDE_COIN_IFACE);
  if (GetBoolArg("-rescan"))
    pindexRescan = USDEBlock::pindexGenesisBlock;
  else
  {
    LOCK(cs_wallet);

    CWalletDB walletdb("usde_wallet.dat");
    CBlockLocator locator(GetCoinIndex(iface));
    if (walletdb.ReadBestBlock(locator))
      pindexRescan = locator.GetBlockIndex();
		walletdb.Close();
  }
  CBlockIndex *pindexBest = GetBestBlockIndex(USDE_COIN_IFACE);
  if (pindexBest != pindexRescan && pindexBest && pindexRescan && pindexBest->nHeight > pindexRescan->nHeight)
  {
    int64 nStart;

    Debug("(usde) LoadWallet: Rescanning last %i blocks (from block %i)...\n", pindexBest->nHeight - pindexRescan->nHeight, pindexRescan->nHeight);
    nStart = GetTimeMillis();
    usdeWallet->ScanForWalletTransactions(pindexRescan, true);
  }
#endif

  // Add wallet transactions that aren't already in a block to mapTransactions
  usdeWallet->ReacceptWalletTransactions(); 

  return (true);
}

#ifdef USE_LEVELDB_COINDB
void USDEWallet::RelayWalletTransaction(CWalletTx& wtx)
{
  USDETxDB txdb;

  BOOST_FOREACH(const CMerkleTx& tx, wtx.vtxPrev) 
  { 
    if (!tx.IsCoinBase())
    {
      uint256 hash = tx.GetHash();
      if (!txdb.ContainsTx(hash))
        RelayMessage(CInv(ifaceIndex, MSG_TX, hash), (CTransaction)tx);
    }
  }

  if (!wtx.IsCoinBase())
  {
    uint256 hash = wtx.GetHash();
    if (!txdb.ContainsTx(hash))
    {
      RelayMessage(CInv(ifaceIndex, MSG_TX, hash), (CTransaction)wtx);
    }
  }

  txdb.Close();
}
#else
void USDEWallet::RelayWalletTransaction(CWalletTx& wtx)
{
  CIface *iface = GetCoinByIndex(USDE_COIN_IFACE);

  BOOST_FOREACH(const CMerkleTx& tx, wtx.vtxPrev) { 
    if (!tx.IsCoinBase() && !tx.vin.empty()) {
      uint256 hash = tx.GetHash();
      if (!VerifyTxHash(iface, hash)) {
        RelayMessage(CInv(ifaceIndex, MSG_TX, hash), (CTransaction)tx);
      }
    }
  }

  if (!wtx.IsCoinBase()) {
    uint256 hash = wtx.GetHash();
    if (!VerifyTxHash(iface, hash)) {
      RelayMessage(CInv(ifaceIndex, MSG_TX, hash), (CTransaction)wtx);
    }
  }

}
#endif


void USDEWallet::ResendWalletTransactions()
{
  CIface *iface = GetCoinByIndex(USDE_COIN_IFACE);
  CTxMemPool *pool = GetTxMemPool(iface);

  // Do this infrequently and randomly to avoid giving away
  // that these are our transactions.
  static int64 nNextTime;
  if (GetTime() < nNextTime)
    return;
  bool fFirst = (nNextTime == 0);
  nNextTime = GetTime() + GetRand(30 * 60);
  if (fFirst)
    return;

  // Only do it if there's been a new block since last time
  static int64 nLastTime;
  if (USDEBlock::nTimeBestReceived < nLastTime)
    return;
  nLastTime = GetTime();

  // Rebroadcast any of our txes that aren't in a block yet
  {
    LOCK(cs_wallet);
    // Sort them in chronological order
    multimap<unsigned int, CWalletTx*> mapSorted;
    BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, mapWallet)
    {
      CWalletTx& wtx = item.second;
      const uint256& tx_hash = item.first;
      if (!pool->exists(tx_hash))
        continue;
      // Don't rebroadcast until it's had plenty of time that
      // it should have gotten in already by now.
      if (USDEBlock::nTimeBestReceived - (int64)wtx.nTimeReceived > 5 * 60)
        mapSorted.insert(make_pair(wtx.nTimeReceived, &wtx));
    }
    BOOST_FOREACH(PAIRTYPE(const unsigned int, CWalletTx*)& item, mapSorted)
    {
      CWalletTx& wtx = *item.second;
//      wtx.RelayWalletTransaction(txdb);
      RelayWalletTransaction(wtx);
    }
  }
}

void USDEWallet::ReacceptWalletTransactions()
{
	{
		LOCK(cs_wallet);
		core_ReacceptWalletTransactions(this);
	}
#if 0
  CIface *iface = GetCoinByIndex(ifaceIndex);
  blkidx_t *blockIndex = GetBlockTable(ifaceIndex);
  vector<CWalletTx> vMissingTx;
  bool fRepeat = true;
  bool spent;
	int i;

  {
    LOCK(cs_wallet);
    fRepeat = false;
    BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, mapWallet)
    {
      CWalletTx& wtx = item.second;
      vector<uint256> vOuts; 

			for (i = 0; i < wtx.vfSpent.size(); i++) {
				if (wtx.vfSpent[i])
					break;
			}
			if (i != wtx.vfSpent.size())
				continue; /* already [at least partially] spent. */
/* DEBUG: TODO: need to be careful here to still add supporting tx's */

      if (!wtx.IsCoinBase() &&
          !VerifyTxHash(iface, wtx.GetHash())) { /* !block-chain */
        /* reaccept into mempool. */
        if (!wtx.AcceptWalletTransaction()) {
          error(SHERR_INVAL, "ReacceptWalletTransactions: !wtx.AcceptWalletTransaction()");
        }
      } else if (wtx.ReadCoins(ifaceIndex, vOuts)) {
        /* sanity */
        if (vOuts.size() != wtx.vout.size()) {
          error(SHERR_INVAL, "ReacceptWalletTransactions: txindex.vSpent.size() %d != wtx.vout.size() %d\n", vOuts.size(), wtx.vout.size());
          continue;
        }

        /* check for mis-marked spents */
        if (wtx.vfSpent.size() < wtx.vout.size()) {
          wtx.vfSpent.resize(wtx.vout.size());
          wtx.MarkDirty();
        }
        for (unsigned int i = 0; i < vOuts.size(); i++) {
          if (vOuts[i].IsNull())
            continue; /* not spent */

          if (!IsMine(wtx.vout[i]))
            continue; /* not local */

          spent = !vOuts[i].IsNull();
          if (spent != wtx.vfSpent[i]) {
            /* over-ride wallet with coin db */
            wtx.vfSpent[i] = spent;
            vMissingTx.push_back(wtx);
          }
        }
      }
    }

    if (!vMissingTx.empty()) { /* mis-marked tx's */
      CBlockIndex *min_pindex = NULL;
      int64 min_height = GetBestHeight(iface) + 1;
      BOOST_FOREACH(CWalletTx& wtx, vMissingTx) {
        uint256 hash = wtx.GetHash();

        Debug("ReacceptWalletTransaction: found spent coin (%f) \"%s\".", FormatMoney(wtx.GetCredit()).c_str(), hash.ToString().c_str());

        /* update to disk */
        wtx.MarkDirty();
        wtx.WriteToDisk();

        /* find earliest block-index involved. */
        CTransaction t_tx;
        uint256 blk_hash; 
        if (::GetTransaction(iface, hash, t_tx, &blk_hash) &&
            blockIndex->count(blk_hash) != 0) {
          CBlockIndex *pindex = (*blockIndex)[blk_hash];
          if (pindex->nHeight >= min_height) {
            min_pindex = pindex;
            min_height = pindex->nHeight;
          }
        }
      }
      if (min_pindex) {
        if (min_pindex->pprev)
          min_pindex = min_pindex->pprev;
        ScanForWalletTransactions(min_pindex);
      } else {
        ScanForWalletTransactions(USDEBlock::pindexGenesisBlock);
      }
    }
  }
#endif
}

int USDEWallet::ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate)
{
    int ret = 0;

#if 0
    CBlockIndex* pindex = pindexStart;
    {
        LOCK(cs_wallet);
        while (pindex)
        {
            USDEBlock block;
            block.ReadFromDisk(pindex, true);
            BOOST_FOREACH(CTransaction& tx, block.vtx)
            {
                if (AddToWalletIfInvolvingMe(tx, &block, fUpdate))
                    ret++;
            }
            pindex = pindex->pnext;
        }
    }
#endif

    if (pindexStart)
      InitServiceWalletEvent(this, pindexStart->nHeight);

    return ret;
}

int64 USDEWallet::GetTxFee(CTransaction tx)
{
  int64 nFees;
  int i;

  if (tx.IsCoinBase())
    return (0);
  CIface *iface = GetCoinByIndex(USDE_COIN_IFACE);
  CBlock *pblock = GetBlockByTx(iface, tx.GetHash());

  nFees = 0;
#ifdef USE_LEVELDB_COINDB
  map<uint256, CTxIndex> mapQueuedChanges;
  MapPrevTx inputs;
  USDETxDB txdb;
  bool fInvalid = false;
  if (tx.FetchInputs(txdb, mapQueuedChanges, pblock, false, inputs, fInvalid))
    nFees += tx.GetValueIn(inputs) - tx.GetValueOut();
  txdb.Close();
#else
  tx_cache inputs;
  if (FillInputs(tx, inputs)) {
    nFees += tx.GetValueIn(inputs) - tx.GetValueOut();
  }
#endif

  if (pblock) delete pblock;
  return (nFees);
}


bool USDEWallet::CommitTransaction(CWalletTx& wtxNew)
{
  CIface *iface = GetCoinByIndex(USDE_COIN_IFACE);
  CTxMemPool *pool = GetTxMemPool(iface);

  {
    LOCK2(cs_main, cs_wallet);
    {
      // This is only to keep the database open to defeat the auto-flush for the
      // duration of this scope.  This is the only place where this optimization
      // maybe makes sense; please don't do it anywhere else.
//      CWalletDB* pwalletdb = new CWalletDB(strWalletFile,"r");

      // Add tx to wallet, because if it has change it's also ours,
      // otherwise just for transaction history.
      AddToWallet(wtxNew);

#if 0
      // Mark old coins as spent
      set<CWalletTx*> setCoins;
      BOOST_FOREACH(const CTxIn& txin, wtxNew.vin)
      {
        CWalletTx &coin = mapWallet[txin.prevout.hash];
        coin.BindWallet(this);
        coin.MarkSpent(txin.prevout.n);
        coin.WriteToDisk();
        //NotifyTransactionChanged(this, coin.GetHash(), CT_UPDATED);
      }
#endif

//			delete pwalletdb;
    }

    // Track how many getdata requests our transaction gets
    mapRequestCount[wtxNew.GetHash()] = 0;

#if 0
    // Broadcast
    USDETxDB txdb;
    bool ret = wtxNew.AcceptToMemoryPool(txdb);
    if (ret) {
//      wtxNew.RelayWalletTransaction(txdb);
      RelayWalletTransaction(wtxNew);
    }
    txdb.Close();
    if (!ret) {
      // This must not fail. The transaction has already been signed and recorded.
      printf("CommitTransaction() : Error: Transaction not valid");
      return false;
    }
#endif

    if (!pool->AddTx(wtxNew))
      return (false);

    RelayWalletTransaction(wtxNew);
  }

  STAT_TX_SUBMITS(iface)++;

  return true;
}

#if 0
bool USDEWallet::CreateTransaction(const vector<pair<CScript, int64> >& vecSend, CWalletTx& wtxNew, CReserveKey& reservekey, int64& nFeeRet)
{
  CIface *iface = GetCoinByIndex(USDE_COIN_IFACE);
  int64 nValue = 0;

  BOOST_FOREACH (const PAIRTYPE(CScript, int64)& s, vecSend)
  {
    if (nValue < 0)
      return false;
    nValue += s.second;
  }
  if (vecSend.empty() || nValue < 0)
    return false;

  wtxNew.BindWallet(this);

  {
    LOCK2(cs_main, cs_wallet);
    {
      nFeeRet = nTransactionFee;
      loop
      {
        wtxNew.vin.clear();
        wtxNew.vout.clear();
        wtxNew.fFromMe = true;

        int64 nTotalValue = nValue + nFeeRet;
        double dPriority = 0;
        // vouts to the payees
        BOOST_FOREACH (const PAIRTYPE(CScript, int64)& s, vecSend)
          wtxNew.vout.push_back(CTxOut(s.second, s.first));

        // Choose coins to use
        set<pair<const CWalletTx*,unsigned int> > setCoins;
        int64 nValueIn = 0;
        if (!SelectCoins(nTotalValue, setCoins, nValueIn))
          return false;
        BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int) pcoin, setCoins)
        {
          int64 nCredit = pcoin.first->vout[pcoin.second].nValue;
          dPriority += (double)nCredit * pcoin.first->GetDepthInMainChain(ifaceIndex);
        }

        int64 nChange = nValueIn - nValue - nFeeRet;
        // if sub-cent change is required, the fee must be raised to at least MIN_TX_FEE
        // or until nChange becomes zero
        // NOTE: this depends on the exact behaviour of GetMinFee
        if (nFeeRet < USDE_MIN_TX_FEE && nChange > 0 && nChange < CENT)
        {
          int64 nMoveToFee = min(nChange, USDE_MIN_TX_FEE - nFeeRet);
          nChange -= nMoveToFee;
          nFeeRet += nMoveToFee;
        }

        if (nChange > 0)
        {
          // Note: We use a new key here to keep it from being obvious which side is the change.
          //  The drawback is that by not reusing a previous key, the change may be lost if a
          //  backup is restored, if the backup doesn't have the new private key for the change.
          //  If we reused the old key, it would be possible to add code to look for and
          //  rediscover unknown transactions that were written with keys of ours to recover
          //  post-backup change.

          // Reserve a new key pair from key pool
          CPubKey vchPubKey = reservekey.GetReservedKey();
          // assert(mapKeys.count(vchPubKey));

          // Fill a vout to ourself
          // TODO: pass in scriptChange instead of reservekey so
          // change transaction isn't always pay-to-bitcoin-address
          CScript scriptChange;
          scriptChange.SetDestination(vchPubKey.GetID());

          // Insert change txn at random position:
          vector<CTxOut>::iterator position = wtxNew.vout.begin()+GetRandInt(wtxNew.vout.size());
          wtxNew.vout.insert(position, CTxOut(nChange, scriptChange));
        }
        else
          reservekey.ReturnKey();

        // Fill vin
        BOOST_FOREACH(const PAIRTYPE(const CWalletTx*,unsigned int)& coin, setCoins)
          wtxNew.vin.push_back(CTxIn(coin.first->GetHash(),coin.second));

        unsigned int nIn = 0;
        BOOST_FOREACH(const PAIRTYPE(const CWalletTx*,unsigned int)& coin, setCoins) {
          CSignature sig(USDE_COIN_IFACE, &wtxNew, nIn);
          if (!sig.SignSignature(*coin.first)) {
            return false;
          }

          nIn++;
        }
#if 0
        // Sign
        int nIn = 0;
        BOOST_FOREACH(const PAIRTYPE(const CWalletTx*,unsigned int)& coin, setCoins)
          if (!SignSignature(*this, *coin.first, wtxNew, nIn++)) {
            return false;
          }
#endif

        /* Ensure transaction does not breach a defined size limitation. */
        unsigned int nWeight = GetTransactionWeight(wtxNew);
        if (nWeight >= MAX_TRANSACTION_WEIGHT(iface)) {
          return (error(SHERR_INVAL, "The transaction size is too large."));
        }

        unsigned int nBytes = GetVirtualTransactionSize(wtxNew);
        dPriority /= nBytes;

        // Check that enough fee is included
        int64 nPayFee = nTransactionFee * (1 + (int64)nBytes / 1000);
#if 0
        bool fAllowFree = AllowFree(dPriority);
        int64 nMinFee = wtxNew.GetMinFee(USDE_COIN_IFACE, 1, fAllowFree, GMF_SEND);
#endif
        int64 nMinFee = CalculateFee(wtxNew);

        if (nFeeRet < max(nPayFee, nMinFee))
        {
          nFeeRet = max(nPayFee, nMinFee);
          continue;
        }

        // Fill vtxPrev by copying from previous transactions vtxPrev
        wtxNew.AddSupportingTransactions();
        wtxNew.fTimeReceivedIsTxTime = true;

        break;
      }
    }
  }
  return true;
}
bool USDEWallet::CreateTransaction(CScript scriptPubKey, int64 nValue, CWalletTx& wtxNew, CReserveKey& reservekey, int64& nFeeRet)
{
    vector< pair<CScript, int64> > vecSend;
    vecSend.push_back(make_pair(scriptPubKey, nValue));
    return CreateTransaction(vecSend, wtxNew, reservekey, nFeeRet);
}
#endif


void USDEWallet::AddSupportingTransactions(CWalletTx& wtx)
{
  wtx.AddSupportingTransactions();
}

#ifdef USE_LEVELDB_COINDB
bool USDEWallet::UnacceptWalletTransaction(const CTransaction& tx)
{
  CIface *iface = GetCoinByIndex(USDE_COIN_IFACE);

  if (!core_UnacceptWalletTransaction(iface, tx))
    return (false);

  {
    USDETxDB txdb;

    BOOST_FOREACH(const CTxIn& in, tx.vin) {
      const uint256& prev_hash = in.prevout.hash; 
      int nTxOut = in.prevout.n;

      CTxIndex txindex;
      if (!txdb.ReadTxIndex(prev_hash, txindex))
        continue;

      if (nTxOut >= txindex.vSpent.size())
        continue; /* bad */

      /* set output as unspent */
      txindex.vSpent[nTxOut].SetNull();
      txdb.UpdateTxIndex(prev_hash, txindex);
    }

    /* remove pool tx from db */
    txdb.EraseTxIndex(tx);

    txdb.Close();
  }

  return (true);
}
#else
bool USDEWallet::UnacceptWalletTransaction(const CTransaction& tx)
{
  CIface *iface = GetCoinByIndex(USDE_COIN_IFACE);

  return (core_UnacceptWalletTransaction(iface, tx));
}
#endif

int64 USDEWallet::GetBlockValue(int nHeight, int64 nFees, uint160 hColor)
{
  return (usde_GetBlockValue(nHeight, nFees));
}

bool USDEWallet::CreateAccountTransaction(string strFromAccount, const vector<pair<CScript, int64> >& vecSend, CWalletTx& wtxNew, string& strError, int64& nFeeRet)
{
  CIface *iface = GetCoinByIndex(USDE_COIN_IFACE);

  wtxNew.strFromAccount = strFromAccount;

  int64 nValue = 0;
  BOOST_FOREACH (const PAIRTYPE(CScript, int64)& s, vecSend)
  {
    if (nValue < 0)
      return false;
    nValue += s.second;
  }
  if (vecSend.empty() || nValue < 0)
    return false;

  wtxNew.BindWallet(this);

  {
    LOCK2(cs_main, cs_wallet);
    {
      nFeeRet = nTransactionFee;
      loop
      {
        wtxNew.vin.clear();
        wtxNew.vout.clear();
        wtxNew.fFromMe = true;

        int64 nTotalValue = nValue + nFeeRet;
        double dPriority = 0;
        // vouts to the payees
        BOOST_FOREACH (const PAIRTYPE(CScript, int64)& s, vecSend)
          wtxNew.vout.push_back(CTxOut(s.second, s.first));

        // Choose coins to use
        set<pair<const CWalletTx*,unsigned int> > setCoins;
        int64 nValueIn = 0;
        if (!SelectAccountCoins(strFromAccount, nTotalValue, setCoins, nValueIn)) {
          strError = "An error occurred obtaining sufficient coins in order perform the transaction. Check the transaction fee cost.";
          return false;
        }
        BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int) pcoin, setCoins)
        {
          int64 nCredit = pcoin.first->vout[pcoin.second].nValue;
          dPriority += (double)nCredit * pcoin.first->GetDepthInMainChain(ifaceIndex);
        }

        int64 nChange = nValueIn - nValue - nFeeRet;
        // if sub-cent change is required, the fee must be raised to at least USDE_MIN_TX_FEE
        // or until nChange becomes zero
        // NOTE: this depends on the exact behaviour of GetMinFee
        if (nFeeRet < USDE_MIN_TX_FEE && nChange > 0 && nChange < CENT)
        {
          int64 nMoveToFee = min(nChange, USDE_MIN_TX_FEE - nFeeRet);
          nChange -= nMoveToFee;
          nFeeRet += nMoveToFee;
        }

				if (nChange > 0) {
					CKeyID keyID;
					CCoinAddr addr = GetAccountAddress(this, strFromAccount, true);
					if (addr.GetKeyID(keyID)) {
						CScript scriptChange;
						scriptChange.SetDestination(keyID);

						// Insert change txn at random position:
						vector<CTxOut>::iterator position = wtxNew.vout.begin()+GetRandInt(wtxNew.vout.size());
						wtxNew.vout.insert(position, CTxOut(nChange, scriptChange));
					}
        }

        // Fill vin
        BOOST_FOREACH(const PAIRTYPE(const CWalletTx*,unsigned int)& coin, setCoins)
          wtxNew.vin.push_back(CTxIn(coin.first->GetHash(),coin.second));

        unsigned int nIn = 0;
        BOOST_FOREACH(const PAIRTYPE(const CWalletTx*,unsigned int)& coin, setCoins) {
          CSignature sig(USDE_COIN_IFACE, &wtxNew, nIn);
          const CWalletTx *s_wtx = coin.first;
          if (!sig.SignSignature(*s_wtx)) {

#if 0
            /* failing signing against prevout. mark as spent to prohibit further attempts to use this output. */
            s_wtx->MarkSpent(nIn);
#endif

            strError = strprintf(_("An error occurred signing the transaction [input tx \"%s\", output #%d]."), s_wtx->GetHash().GetHex().c_str(), nIn);
            return false;
          }

          nIn++;
        }
#if 0
        // Sign
        int nIn = 0;
        BOOST_FOREACH(const PAIRTYPE(const CWalletTx*,unsigned int)& coin, setCoins) {
          const CWalletTx *s_wtx = coin.first;
          if (!SignSignature(*this, *s_wtx, wtxNew, nIn++)) {

#if 0
            /* failing signing against prevout. mark as spent to prohibit further attempts to use this output. */
            s_wtx->MarkSpent(nIn);
#endif

            strError = strprintf(_("An error occurred signing the transaction [input tx \"%s\", output #%d]."), s_wtx->GetHash().GetHex().c_str(), nIn);
            return false;
          }
        }
#endif

        /* Ensure transaction does not breach a defined size limitation. */
        unsigned int nWeight = GetTransactionWeight(wtxNew);
        if (nWeight >= MAX_TRANSACTION_WEIGHT(iface)) {
          return (error(SHERR_INVAL, "The transaction size is too large."));
        }

        unsigned int nBytes = GetVirtualTransactionSize(wtxNew);
        dPriority /= nBytes;

        // Check that enough fee is included
        int64 nPayFee = nTransactionFee * (1 + (int64)nBytes / 1000);
#if 0
        bool fAllowFree = AllowFree(dPriority);
        int64 nMinFee = wtxNew.GetMinFee(USDE_COIN_IFACE, 1, fAllowFree, GMF_SEND);
#endif
        int64 nMinFee = CalculateFee(wtxNew);

        if (nFeeRet < max(nPayFee, nMinFee))
        {
          nFeeRet = max(nPayFee, nMinFee);
          continue;
        }

        // Fill vtxPrev by copying from previous transactions vtxPrev
        wtxNew.AddSupportingTransactions();
        wtxNew.fTimeReceivedIsTxTime = true;

        break;
      }
    }
  }
  return true;
}
bool USDEWallet::CreateAccountTransaction(string strFromAccount, CScript scriptPubKey, int64 nValue, CWalletTx& wtxNew, string& strError, int64& nFeeRet)
{
  vector< pair<CScript, int64> > vecSend;
  vecSend.push_back(make_pair(scriptPubKey, nValue));
  return CreateAccountTransaction(strFromAccount, vecSend, wtxNew, strError, nFeeRet);
}


unsigned int USDEWallet::GetTransactionWeight(const CTransaction& tx)
{
  unsigned int nBytes;

  nBytes = ::GetSerializeSize(tx, SER_NETWORK, USDE_PROTOCOL_VERSION);

  return (nBytes);
}

unsigned int USDEWallet::GetVirtualTransactionSize(int64 nWeight, int64 nSigOpCost)
{
  return ((unsigned int)nWeight);
}

unsigned int USDEWallet::GetVirtualTransactionSize(const CTransaction& tx)
{
  return (GetVirtualTransactionSize(GetTransactionWeight(tx), 0));
}


/** Large (in bytes) low-priority (new, small-coin) transactions require fee. */
double USDEWallet::AllowFreeThreshold()
{
  return COIN * 700 / 250;
}

int64 USDEWallet::GetFeeRate(uint160 hColor)
{
  CIface *iface = GetCoinByIndex(USDE_COIN_IFACE);
	return (MIN_TX_FEE_RATE(iface));
}

int USDEWallet::GetCoinbaseMaturity(uint160 hColor)
{
  CIface *iface = GetCoinByIndex(USDE_COIN_IFACE);
	return (iface ? iface->coinbase_maturity : 0);
}

bool USDEWallet::IsAlgoSupported(int alg, CBlockIndex *pindexPrev, uint160 hColor)
{
	if (alg == ALGO_SCRYPT)
		return (true);
	return (false);
}

