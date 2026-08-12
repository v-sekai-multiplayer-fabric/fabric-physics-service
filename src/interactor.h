// The Queen's interactor.
//
// SPDX-License-Identifier: Apache-2.0
#ifndef QUEEN_INTERACTOR_H
#define QUEEN_INTERACTOR_H

#include "ward.h"

#include "weft/cbor.h"
#include "weft/interactor.h"

// The longest command the ward will read. A longer one is dropped rather than truncated,
// because a truncated command is a different command and running one nobody typed is worse
// than answering none.
#define WARD_COMMAND_MAX 512

// The largest reply. A ward that does not fit says so rather than sending a short batch,
// which a reader cannot tell from a complete one.
#define WARD_REPLY_MAX 262144

// Binds the ward to the contract. The returned interactor borrows `g` and does not own it.
weft_interactor_t ward_interactor(gyre_t *g);

#endif
