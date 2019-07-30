
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_FILE_H_INCLUDED_
#define _NJS_FILE_H_INCLUDED_


void njs_file_basename(const njs_str_t *path, njs_str_t *name);
void njs_file_dirname(const njs_str_t *path, njs_str_t *name);


#endif /* _NJS_FILE_H_INCLUDED_ */
