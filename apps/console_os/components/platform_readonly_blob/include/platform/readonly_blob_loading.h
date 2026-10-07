// SPDX-License-Identifier: MIT
#ifndef PLATFORM_READONLY_BLOB_LOADING_H
#define PLATFORM_READONLY_BLOB_LOADING_H
#include <stdbool.h>

/* Optional Console OS loading service. The weak default returns false.
 * Called synchronously by the Arena VFS with its lock held, but between
 * storage reads. A true result requests bounded reads with further service
 * calls between blocks. An override must be safe on the loading task, must
 * not reenter VFS/storage, and must return false before runtime can deliver
 * game events. No callback or caller context is retained. The VFS times this
 * callback only during the optional sprite-header startup hint and emits one
 * aggregate at hint end; ordinary gameplay reads do not collect timing. */
bool platform_readonly_blob_loading_progress(void);

#endif
