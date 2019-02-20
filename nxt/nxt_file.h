
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NXT_FILE_H_INCLUDED_
#define _NXT_FILE_H_INCLUDED_


void nxt_file_basename(const nxt_str_t *path, nxt_str_t *name);
void nxt_file_dirname(const nxt_str_t *path, nxt_str_t *name);


#endif /* _NXT_FILE_H_INCLUDED_ */
