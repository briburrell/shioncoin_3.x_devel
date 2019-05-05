
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

#ifndef __EXEC_H__
#define __EXEC_H__



#define DEFAULT_EXEC_LIFESPAN SHARE_DEFAULT_EXPIRE_TIME /* 48yr */

#define MAX_EXEC_SIZE 780000

class CExecCore : public CExtCore
{

	public:
		/* the originating address of the transaction. */
		uint160 kSender;
		/* the executable code of a SEXE class. */
		cbuff vContext;
		/* a signature certifying the primary contents of the exec/call. */
		CSign signature;

		/* the system time at which the call was initiated. */
		int64 nTime;
		/* the blockchain height at which the call was initiated. */
		int64 nHeight;
		/* the coin value sent to process the method call. */
		int64 nValue;
		/* a hash of the result returned from the method call. */
		uint64 hResult; 
		/* the SEXE executable class. */
		uint160 hExec;
		/* the previous call hash */
		uint160 hPrev;
		/* a checksum hash of the class's user-data. */
		uint256 hData;
		/* the name of the method called. */
		cbuff vchMethod;
		/* the arguments passed into the class method. */
		vector<cbuff> vArg;	
		/* a list of transactions generated by this exec call. */
		vector<uint256> vTx;

    CExecCore()
		{
			SetNull();
		}

    CExecCore(const CExecCore& execIn)
    {
      SetNull();
      Init(execIn);
    }

    void SetNull()
    {
      CExtCore::SetNull();

			kSender = 0;
			vContext.clear();
			signature.SetNull();

			nTime = 0;
			nHeight = 0;
			nValue = 0;
			hResult = 0;
			hExec = 0;
			hPrev = 0;
			hData = 0;
			vchMethod.clear();
			vArg.clear();
			vTx.clear();
    }

    void Init(const CExecCore& execIn)
    {
			SetNull();
			CExtCore::Init((const CExtCore& )execIn);

			kSender = execIn.kSender;
			vContext = execIn.vContext;
			signature = execIn.signature;

			nTime = execIn.nTime;
			nHeight = execIn.nHeight;
			nValue = execIn.nValue;
			hResult = execIn.hResult;
			hExec = execIn.hExec;
			hPrev = execIn.hPrev;
			vchMethod = execIn.vchMethod;
			hData = execIn.hData;

			vArg.clear();
			BOOST_FOREACH(const cbuff& buf, execIn.vArg) {
				vArg.insert(vArg.end(), buf);
			}

			vTx.clear();
			BOOST_FOREACH(const uint256& h, execIn.vTx) {
				vTx.insert(vTx.end(), h);
			}
    }

    IMPLEMENT_SERIALIZE (
      READWRITE(*(CExtCore *)this);

			READWRITE(kSender);
			READWRITE(vContext);
			READWRITE(signature);

			READWRITE(nTime);
			READWRITE(nHeight);
			READWRITE(nValue);
			READWRITE(hResult);
			READWRITE(hExec);
			READWRITE(hPrev);
			READWRITE(hData);
			READWRITE(vchMethod);
			READWRITE(vArg);
			READWRITE(vTx);
    )

    friend bool operator==(const CExecCore &a, const CExecCore &b)
    {
			if (a.vArg.size() != b.vArg.size())
				return (false);
			for (int i = 0; i < b.vArg.size(); i++) {
				if (a.vArg[i] != b.vArg[i])
					return (false);
			}
			if (a.vTx.size() != b.vTx.size())
				return (false);
			for (int i = 0; i < b.vTx.size(); i++) {
				if (a.vTx[i] != b.vTx[i])
					return (false);
			}
      return (
        ((CExtCore&) a) == ((CExtCore&) b) &&
				a.kSender == b.kSender &&
				a.vContext == b.vContext &&
				a.signature == b.signature &&
				a.nTime == b.nTime &&
				a.nValue == b.nValue &&
				a.hResult == b.hResult &&
				a.hExec == b.hExec &&
				a.hPrev == b.hPrev &&
				a.hData == b.hData &&
				a.vchMethod == b.vchMethod
				);
    }

		CKeyID GetSenderKey()
		{
			return (CKeyID(kSender));
		}

		CCoinAddr GetSenderAddr(int ifaceIndex);

    const uint160 GetHash()
    {
      uint256 hashOut = SerializeHash(*this);
      unsigned char *raw = (unsigned char *)&hashOut;
      cbuff rawbuf(raw, raw + sizeof(hashOut));
      return Hash160(rawbuf);
    }

};

class CExec : public CExecCore
{

  public:

    CExec()
    {
      SetNull();
    }

    CExec(const CExec& execIn)
    {
      SetNull();
      Init(execIn);
    }

    IMPLEMENT_SERIALIZE (
      READWRITE(*(CExecCore *)this);
    )

    void SetNull()
    {
      CExecCore::SetNull();
    }

    void Init(const CExec& execIn)
    {
			CExecCore::Init((const CExecCore& )execIn);
    }

    friend bool operator==(const CExec &a, const CExec &b)
    {
      return (
        ((CExecCore&) a) == ((CExecCore&) b)
				);
    }

    CExec operator=(const CExec &b)
    {
      Init(b);
      return (*this);
    }

    bool Sign(int ifaceIndex, CCoinAddr& addr);

    bool VerifySignature(int ifaceIndex);

    bool LoadData(string path, cbuff& data);

    bool SetStack(cbuff stack)
    {
      vContext = stack;
      return (true);
    }

    cbuff GetStack()
    { 
      return (vContext);
    }


    bool SetSenderAddr(CCoinAddr& addr);

		bool VerifyStack(int ifaceIndex);

		bool CallStack(int ifaceIndex, CCoinAddr sendAddr, shjson_t **param_p);

		bool GetStackData(int ifaceIndex, shjson_t **j_p);

		int64 GetStackHeight(int ifaceIndex);

		string GetClassName()
		{
			return (GetLabel());
		}

    const uint160 GetHash()
    {
			return (CExecCore::GetHash());
    }

    std::string ToString(int ifaceIndex);

    Object ToValue(int ifaceIndex);

};

class CExecCall : public CExec
{

  public:

    CExecCall()
    {
      SetNull();
    }

    CExecCall(const CExecCall& execIn)
    {
      SetNull();
      Init(execIn);
    }

		CExecCall(const CExec& exec)
		{
			SetNull();
			CExec::Init((const CExec& )exec);
		}

    IMPLEMENT_SERIALIZE (
      READWRITE(*(CExec *)this);
    )

    void SetNull()
    {
      CExec::SetNull();
    }

    void Init(const CExecCall& execIn)
    {
			CExec::Init((const CExec& )execIn);
    }

    friend bool operator==(const CExecCall &a, const CExecCall &b)
    {
      return (
          ((CExec&) a) == ((CExec&) b)
          );
    }

    CExecCall operator=(const CExecCall &b)
    {
      Init(b);
      return (*this);
    }

    bool Sign(int ifaceIndex, CCoinAddr& addr);

    bool VerifySignature(int ifaceIndex);

    int64 GetSendValue()
    {
      return (nValue);
    }

    void SetSendValue(int64 nValueIn)
    {
      nValue = nValueIn;
    }

		time_t GetSendTime()
		{
			return (nTime);
		}

		void SetSendTime();

		int64 GetCommitHeight()
		{
			return (nHeight);
		}

		void SetCommitHeight(int64 nHeightIn)
		{
			nHeight = nHeightIn; 
		}

		void SetCommitHeight(int ifaceIndex);

		const uint64 GetResultHash()
		{
			return (hResult);
		}

		void SetResultHash(uint64 hResultIn)
		{
			hResult = hResultIn;
		}

		const uint160 GetExecHash()
		{
			return (hExec);
		}

		const uint160 GetPrevHash()
		{
			return (hPrev);
		}

		string GetMethodName()
		{
			return (stringFromVch(vchMethod));
		}

		void SetMethodName(string text)
		{
			vchMethod = vchFromString(text);
		}

		uint256 GetChecksum()
		{
			return (hData);
		}

		void SetChecksum(string text)
		{
			hData = uint256(text);
		}

		void SetChecksum(const uint256 hDataIn)
		{
			hData = hDataIn;
		}

		bool GetExec(int ifaceIndex, CExec& execOut);

#if 0
		void SetSignContext();

		cbuff GetSignContext()
		{
			return (vContext);
		}
#endif

		void AddTxChain(uint256 hash)
		{
			vTx.insert(vTx.end(), hash);
		}

		const vector<uint256> GetTxChain()
		{
			return (vTx);
		}

		const uint256 GetTxChainHash()
		{
      uint256 hashOut = SerializeHash(vTx);
			return (hashOut);
		}

		/* create a 'coinbase' tx reference for signing. */
		void InitTxChain();

    const uint160 GetHash()
    {
			return (CExec::GetHash());
    }

    std::string ToString(int ifaceIndex);

    Object ToValue(int ifaceIndex);
};

class CExecCheckpoint : public CExecCore
{

  public:

    CExecCheckpoint()
    {
      SetNull();
    }

    CExecCheckpoint(const CExecCheckpoint& execIn)
    {
      SetNull();
      Init(execIn);
    }

    IMPLEMENT_SERIALIZE (
      READWRITE(*(CExecCore *)this);
    )

    void SetNull()
    {
      CExecCore::SetNull();
    }

    void Init(const CExecCheckpoint& execIn)
    {
			CExecCore::Init((const CExecCore& )execIn);
    }

    friend bool operator==(const CExecCheckpoint &a, const CExecCheckpoint &b)
    {
      return (
        ((CExecCore&) a) == ((CExecCore&) b)
				);
    }

    CExecCheckpoint operator=(const CExecCheckpoint &b)
    {
      Init(b);
      return (*this);
    }

		uint256 GetChecksum()
		{
			return (hData);
		}

		void SetChecksum(string text)
		{
			hData = uint256(text);
		}

		void SetChecksum(const uint256 hDataIn)
		{
			hData = hDataIn;
		}

		int64 GetCommitHeight()
		{
			return (nHeight);
		}

		void SetCommitHeight(int64 nHeightIn)
		{
			nHeight = nHeightIn; 
		}

		void SetCommitHeight(int ifaceIndex);

		time_t GetSendTime()
		{
			return (nTime);
		}

		void SetSendTime();

		const uint160 GetExecHash()
		{
			return (hExec);
		}

		const uint160 GetPrevHash()
		{
			return (hPrev);
		}

		bool VerifyChecksum(int ifaceIndex);

		bool GetExec(int ifaceIndex, CExec& execOut);

    const uint160 GetHash()
    {
			return (CExecCore::GetHash());
    }



		bool verifyTxChain(int ifaceIndex);

    std::string ToString(int ifaceIndex);

    Object ToValue(int ifaceIndex);

};

bool VerifyExec(CTransaction& tx, int& mode);

int64 GetExecOpFee(CIface *iface, int nHeight, int nSize = MAX_EXEC_SIZE);

int init_exec_tx(CIface *iface, string strAccount, string strPath, CWalletTx& wtx);

int update_exec_tx(CIface *iface, const uint160& hashExec, CWalletTx& wtx);

int generate_exec_tx(CIface *iface, string strAccount, string strClass, int64 nFee, string strFunc, char **args, Value& resp, CWalletTx& wtx);

int activate_exec_tx(CIface *iface, uint160 hExec, uint160 hCert, CWalletTx& wtx);

int transfer_exec_tx(CIface *iface, uint160 hExec, string strAccount, CWalletTx& wtx);

int remove_exec_tx(CIface *iface, const uint160& hashExec, CWalletTx& wtx);

void exec_write_base_object(char *path);

exec_label_list *GetExecLabelTable(int ifaceIndex);

exec_call_list *GetExecCallTable(int ifaceIndex);

exec_call_list *GetExecCallPendingTable(int ifaceIndex);

bool GetExecByLabel(CIface *iface, string strLabel, CExec& execIn);

bool IsExecTx(const CTransaction& tx, int& mode);

int ProcessExecTx(CIface *iface, CNode *pfrom, CTransaction& tx, int64 nHeight);

int DisconnectExecTx(CIface *iface, CTransaction& tx, int mode);

bool GetCallByHash(CIface *iface, const uint160& hCall, CExecCall& callOut);

bool GetExecByHash(CIface *iface, const uint160& hExec, CExec& execOut);

bool GetTxOfExec(CIface *iface, const uint160& hashExec, CTransaction& tx);

bool IsExecCallHash(CIface *iface, const uint160& hExec, const uint160& hCall);

bool IsExecPending(CIface *iface, const uint160& hExec);

bool InsertExecCallPendingTable(CIface *iface, const uint160& hExec, const CTransaction& tx);

bool InsertExecCallTable(CIface *iface, const uint160& hExec, const CTransaction& tx);

bool InsertExecTable(CIface *iface, const CTransaction& tx);

void ResetExecChain(CIface *iface, const uint160& hExec);

int update_exec_tx(CIface *iface, string strAccount, const uint160& hExec, CWalletTx& wtx);

bool ExecRestoreCheckpoint(CIface *iface, const uint160& hExec);

bool ExecRestoreCheckpoint(CIface *iface, CExec *exec);


#endif /* ndef __EXEC_H__ */

