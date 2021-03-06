
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
#include "block.h"
#include "db.h"
#include <vector>

using namespace std;

#define GL_VERSION "version"
#define GL_BESTCHAIN "hashBestChain"

const char *ReadGlobalVar(char *tag, char *var)
{
  char name[1024];
  sprintf(name, "shcoind.%s.%s", tag, var);
  return (shpref_get(name, "")); 
}

void WriteGlobalVar(char *tag, char *var, const char *value)
{
  char name[1024];
  sprintf(name, "shcoind.%s.%s", tag, var);
  shpref_set(name, (char *)value);
}

int ReadVersion(CIface *iface)
{
  const char *str = ReadGlobalVar(iface->name, GL_VERSION);
  return (atoi(str));
}

void WriteVersion(CIface *iface, int nVersion)
{
  char buf[32];
  sprintf(buf, "%u", nVersion);
  WriteGlobalVar(iface->name, GL_VERSION, (const char *)buf);
}

uint256 ReadBestChain(CIface *iface)
{
  const char *str = ReadGlobalVar(iface->name, GL_BESTCHAIN);
  string sHex(str);
  uint256 hash;

  hash.SetHex(sHex);
  return (hash);
}

void WriteBestChain(CIface *iface, uint256 hash)
{
  WriteGlobalVar(iface->name, GL_BESTCHAIN, hash.GetHex().c_str());
}



