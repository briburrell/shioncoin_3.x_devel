
/*
 * @copyright
 *
 *  Copyright 2015 Neo Natura
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

static unet_bind_t _unet_bind[MAX_UNET_MODES];

unet_bind_t *unet_bind_table(int mode)
{
  if (mode < 0 || mode >= MAX_UNET_MODES)
    return (NULL);
  return (_unet_bind + mode);
}

int unet_bind(int mode, int port, char *host)
{
  shpeer_t *peer;
  char hostname[MAXHOSTNAMELEN+1];
  char errbuf[256];
  int err;
  int sk;

	if (host && !*host)
		host = NULL;

  if (_unet_bind[mode].fd != UNDEFINED_SOCKET)
    return (0); /* already bound */

  sk = shnet_sk();
  if (sk == -1)
    return (-errno);

	err = shnet_bindsk(sk, host, port);
  if (err) {
    sprintf(errbuf, "unet_bind: warning: error binding to port %d\n", port);
    unet_log(mode, errbuf);
    return (err);
  }

  _unet_bind[mode].fd = sk;
  _unet_bind[mode].port = port;
  _unet_bind[mode].scan_stamp = shtime();
  _unet_bind[mode].scan_freq = 0.025;

  sprintf(hostname, "127.0.0.1 %d", port);
  peer = shpeer_init(unet_mode_label(mode), hostname);
  memcpy(&_unet_bind[mode].peer, peer, sizeof(shpeer_t));
  shpeer_free(&peer);

  descriptor_claim(sk, mode, DF_LISTEN);

	/* init thread for calling timer */
	unet_thread_init(mode);

  return (0);
}

void unet_connop_set(int mode, unet_addr_op accept_op)
{
  if (mode < 0 || mode >= MAX_UNET_MODES)
    return; 
  _unet_bind[mode].op_accept = accept_op;
}

void unet_disconnop_set(int mode, unet_addr_op close_op)
{
  if (mode < 0 || mode >= MAX_UNET_MODES)
    return; 
  _unet_bind[mode].op_close = close_op;
}

int unet_unbind(int mode)
{
  int err;

  if (_unet_bind[mode].fd == UNDEFINED_SOCKET)
    return (SHERR_INVAL);
	
	/* terminate timer thread */
	unet_thread_free(mode);

  /* close all accepted sockets for coin service. */
  unet_close_all(mode);

  /* close listen (bind) socket */
  descriptor_release(_unet_bind[mode].fd);

  if (_unet_bind[mode].peer_db) {
    bc_close(_unet_bind[mode].peer_db);
    _unet_bind[mode].peer_db = NULL;
  }

  /* clear variables */
  _unet_bind[mode].fd = UNDEFINED_SOCKET;

  return (err);
}

void unet_bind_flag_set(int mode, int flags)
{
  if (mode < 0 || mode >= MAX_UNET_MODES)
    return; 
  _unet_bind[mode].flag |= flags;

  if (flags & UNETF_PEER_SCAN) {
    unet_peer_prune(mode);
    unet_peer_fill(mode);
  }
}

void unet_bind_flag_unset(int mode, int flags)
{
  if (mode < 0 || mode >= MAX_UNET_MODES)
    return; 
  _unet_bind[mode].flag &= ~flags;
}
