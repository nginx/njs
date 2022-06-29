
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 *
 * addr2line impementaton based upon the work by Jeff Muizelaar.
 *
 * A hacky replacement for backtrace_symbols in glibc
 *
 * backtrace_symbols in glibc looks up symbols using dladdr which is limited in
 * the symbols that it sees. libbacktracesymbols opens the executable and
 * shared libraries using libbfd and will look up backtrace information using
 * the symbol table and the dwarf line information.
 *
 * Derived from addr2line.c from GNU Binutils by Jeff Muizelaar
 *
 * Copyright 2007 Jeff Muizelaar
 *
 * addr2line.c -- convert addresses to line number and function name
 * Copyright 1997, 1998, 1999, 2000, 2001, 2002 Free Software Foundation, Inc.
 * Contributed by Ulrich Lauther <Ulrich.Lauther@mchp.siemens.de>
 *
 * This file was part of GNU Binutils.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#define _GNU_SOURCE
#include <njs_main.h>
#include <njs_addr2line.h>

#include <bfd.h>
#include <link.h>


typedef struct {
    const char    *file;
    ElfW(Addr)    address;
    ElfW(Addr)    base;
    void          *hdr;
} njs_file_match_t;


typedef struct {
    bfd_vma       pc;
    const char    *filename;
    const char    *functionname;
    unsigned int  line;
    njs_bool_t    found;
    asymbol       **syms;
} njs_translate_address_t;


static u_char *njs_process_file(u_char *buf, u_char *end, bfd_vma *addr,
    const char *file_name);
static long njs_read_symtab(bfd *abfd, asymbol ***syms);
static u_char *njs_translate_address(u_char *buf, u_char *end, bfd_vma *addr,
    bfd *abfd, asymbol **syms);
static void njs_find_address_in_section(bfd *abfd, asection *section,
    void *data);
static int njs_find_matching_file(struct dl_phdr_info *info, size_t size,
    void *data);


u_char *
_njs_addr2line(u_char *buf, u_char *end, void *address)
{
    bfd_vma           addr;
    const char        *fname;

    njs_file_match_t  match = { .address = (ElfW(Addr)) address };

    bfd_init();

    dl_iterate_phdr(njs_find_matching_file, &match);

    fname = "/proc/self/exe";
    if (match.file != NULL && njs_strlen(match.file)) {
        fname = match.file;
    }

    addr = (ElfW(Addr)) address - match.base;

    return njs_process_file(buf, end, &addr, fname);
}


static u_char *
njs_process_file(u_char *buf, u_char *end, bfd_vma *addr, const char *file_name)
{
    bfd     *abfd;
    char    **matching;
    u_char  *p;
    asymbol **syms;

    abfd = bfd_openr(file_name, NULL);
    if (abfd == NULL) {
        njs_stderror("%s: failed to open while looking for addr2line",
                     file_name);
        return NULL;
    }

    if (bfd_check_format(abfd, bfd_archive)) {
        njs_stderror("%s: can not get addresses from archive", file_name);
        return NULL;
    }

    if (!bfd_check_format_matches(abfd, bfd_object, &matching)) {
        njs_stderror("%s: bfd_check_format_matches() failed",
                     bfd_get_filename(abfd));
        return NULL;
    }

    if (njs_read_symtab(abfd, &syms) <= 0) {
        njs_stderror("%s: njs_read_symtab() failed",
                     bfd_get_filename(abfd));
        return NULL;
    }

    p = njs_translate_address(buf, end, addr, abfd, syms);

    if (syms != NULL) {
        free(syms);
        syms = NULL;
    }

    bfd_close(abfd);

    return p;
}


static long
njs_read_symtab(bfd *abfd, asymbol ***syms)
{
    long      symcount;
    unsigned  size;

    if ((bfd_get_file_flags(abfd) & HAS_SYMS) == 0) {
        return 0;
    }

    symcount = bfd_read_minisymbols(abfd, 0, (PTR) syms, &size);
    if (symcount == 0) {
        symcount = bfd_read_minisymbols(abfd, 1 /* dynamic */,
                                        (PTR) syms, &size);
    }

    return symcount;
}


static u_char *
njs_translate_address(u_char *buf, u_char *end, bfd_vma *addr, bfd *abfd,
    asymbol **syms)
{
    char                     *h;
    const char               *name;
    njs_translate_address_t  ctx;

    ctx.pc = *addr;
    ctx.found = 0;
    ctx.syms = syms;

    bfd_map_over_sections(abfd, njs_find_address_in_section, &ctx);

    if (!ctx.found) {
        return njs_sprintf(buf, end, "\?\? \t\?\?:0 [0x%p]", addr);
    }

    name = ctx.functionname;

    if (name == NULL || *name == '\0') {
        name = "??";
    }

    if (ctx.filename != NULL) {
        h = strrchr(ctx.filename, '/');
        if (h != NULL) {
            ctx.filename = h + 1;
        }
    }

    return njs_sprintf(buf, end, "%s() %s:%ud [0x%p]", name,
                       ctx.filename ? ctx.filename : "??", ctx.line, addr);
}


static void
njs_find_address_in_section(bfd *abfd, asection *section, void *data)
{
    bfd_vma                  vma;
    bfd_size_type            size;
    njs_translate_address_t  *ctx;

    ctx = data;

    if (ctx->found) {
        return;
    }

    if ((bfd_section_flags(section) & SEC_ALLOC) == 0) {
        return;
    }

    vma = bfd_section_vma(section);
    if (ctx->pc < vma) {
        return;
    }

    size = bfd_section_size(section);
    if (ctx->pc >= vma + size) {
        return;
    }

    ctx->found = bfd_find_nearest_line(abfd, section, ctx->syms, ctx->pc - vma,
                                       &ctx->filename, &ctx->functionname,
                                       &ctx->line);
}


static int
njs_find_matching_file(struct dl_phdr_info *info, size_t size, void *data)
{
    long              n;
    const ElfW(Phdr)  *phdr;

    ElfW(Addr)        load_base = info->dlpi_addr;
    njs_file_match_t  *match = data;

    /*
     * This code is modeled from Gfind_proc_info-lsb.c:callback()
     * from libunwind.
     */

    phdr = info->dlpi_phdr;

    for (n = info->dlpi_phnum; --n >= 0; phdr++) {
        if (phdr->p_type == PT_LOAD) {
            ElfW(Addr) vaddr = phdr->p_vaddr + load_base;

            if (match->address >= vaddr
                && match->address < vaddr + phdr->p_memsz)
            {
                /* we found a match */
                match->file = info->dlpi_name;
                match->base = info->dlpi_addr;
            }
        }
    }

    return 0;
}
