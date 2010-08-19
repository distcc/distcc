/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 * Copyright 2007 Google Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
*/

/* Author: Fergus Henderson */

/*
 * fix_debug_info.cc:
 * Performs search-and-replace in the debug info section of an ELF file.
 */

#include <config.h>

#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#ifdef HAVE_ELF_H
  #include <elf.h>
#endif

#include <sys/stat.h>
#ifdef HAVE_SYS_MMAN_H
  #include <sys/mman.h>
#endif

#include "trace.h"
#include "fix_debug_info.h"

/* XINDEX isn't defined everywhere, but where it is, it's always the
 * same as HIRESERVE, so I think this should be safe.
*/
#ifndef SHN_XINDEX
  #define SHN_XINDEX  SHN_HIRESERVE
#endif

#ifdef HAVE_ELF_H
/*
 * Search for an ELF section of the specified name and type.
 * Given an ELF file that has been mmapped (or read) into memory starting
 * at @p elf_mapped_base, find the section with the desired name and type,
 * and return (via the parameters) its start point and size.
 * Returns 1 if found, 0 otherwise.
 */
static int FindElfSection(const void *elf_mapped_base, off_t elf_size,
                           const char *desired_section_name,
                           const void **section_start, int *section_size) {
  const unsigned char *elf_base = (const unsigned char *) elf_mapped_base;
  /* The double cast below avoids warnings with -Wcast-align. */
  const Elf32_Ehdr *elf32_header = (const Elf32_Ehdr *) (const void *) elf_base;
  unsigned int i;
  unsigned int num_sections;

  assert(elf_mapped_base);
  assert(section_start);
  assert(section_size);

  *section_start = NULL;
  *section_size = 0;

  /*
   * There are two kinds of ELF files, 32-bit and 64-bit.  They have similar
   * but slightly different file structures.  It's OK to use the elf32_header
   * structure at this point, prior to checking whether this file is a 32 or
   * 64 bit ELF file, so long as we only access the e_ident field, because
   * the layout of the e_ident field is the same for both kinds: it's the
   * first field in the struct, so its offset is zero, and its size is the
   * same for both 32 and 64 bit ELF files.
   *
   * The magic number which identifies an ELF file is stored in the
   * first few bytes of the e_ident field, which is also the first few
   * bytes of the file.
   */

  if (elf_size < SELFMAG || memcmp(elf32_header, ELFMAG, SELFMAG) != 0) {
    rs_trace("object file is not an ELF file");
    return 0;
  }

  /*
   * The ELF file layouts are defined using fixed-size data structures
   * in <elf.h>, so we don't need to worry about the host computer's
   * word size.  But we do need to worry about the host computer's
   * enddianness, because ELF header fields use the same endianness
   * as the target computer.  When cross-compiling to a target with
   * a different endianness, we would need to byte-swap all the fields
   * that we use.  Right now we don't handle that case.
   *
   * TODO(fergus):
   * handle object files with different endianness than the host.
   */
#if WORDS_BIGENDIAN
  if (elf32_header->e_ident[EI_DATA] != ELFDATA2MSB) {
    rs_trace("sorry, not fixing debug info: "
            "distcc server host is big-endian, object file is not");
    return 0;
  }
#else
  if (elf32_header->e_ident[EI_DATA] != ELFDATA2LSB) {
    rs_trace("sorry, not fixing debug info: "
            "distcc server host is little-endian, object file is not");
    return 0;
  }
#endif

  /*
   * Warning: the following code section is duplicated:
   * once for 32-bit ELF files, and again for 64-bit ELF files.
   * Please be careful to keep them consistent!
   */
  if (elf32_header->e_ident[EI_CLASS] == ELFCLASS32) {
    const Elf32_Ehdr *elf_header = elf32_header;
    const Elf32_Shdr *sections =
        /* The double cast below avoids warnings with -Wcast-align. */
        (const Elf32_Shdr *) (const void *) (elf_base + elf_header->e_shoff);
    const Elf32_Shdr *string_section = sections + elf_header->e_shstrndx;
    const Elf32_Shdr *desired_section = NULL;

    if (elf_size < (off_t) sizeof(*elf_header)) {
      rs_trace("object file is too small for ELF header; maybe got truncated?");
      return 0;
    }
    if (elf_header->e_shoff <= 0 ||
        elf_header->e_shoff > elf_size - sizeof(Elf32_Shdr)) {
      rs_trace("invalid e_shoff value in ELF header");
      return 0;
    }
    if (elf_header->e_shstrndx == SHN_UNDEF) {
      rs_trace("object file has no section name string table"
               " (e_shstrndx == SHN_UNDEF)");
      return 0;
    }
    /* Special case for more sections than will fit in e_shstrndx. */
    if (elf_header->e_shstrndx == SHN_XINDEX) {
      string_section = sections + sections[0].sh_link;
    }
    num_sections = elf_header->e_shnum;
    /* Special case for more sections than will fit in e_shnum. */
    if (num_sections == 0) {
      num_sections = sections[0].sh_size;
    }
    for (i = 0; i < num_sections; ++i) {
      const char *section_name = (char *)(elf_base +
                                          string_section->sh_offset +
                                          sections[i].sh_name);
      if (!strcmp(section_name, desired_section_name)) {
        desired_section = &sections[i];
        break;
      }
    }
    if (desired_section != NULL && desired_section->sh_size > 0) {
      int desired_section_size = desired_section->sh_size;
      *section_start = elf_base + desired_section->sh_offset;
      *section_size = desired_section_size;
      return 1;
    } else {
      return 0;
    }
  } else if (elf32_header->e_ident[EI_CLASS] == ELFCLASS64) {
    /* The double cast below avoids warnings with -Wcast-align. */
    const Elf64_Ehdr *elf_header = (const Elf64_Ehdr *) (const void *) elf_base;
    const Elf64_Shdr *sections =
        (const Elf64_Shdr *) (const void *) (elf_base + elf_header->e_shoff);
    const Elf64_Shdr *string_section = sections + elf_header->e_shstrndx;
    const Elf64_Shdr *desired_section = NULL;

    if (elf_size < (off_t) sizeof(*elf_header)) {
      rs_trace("object file is too small for ELF header; maybe got truncated?");
      return 0;
    }
    if (elf_header->e_shoff <= 0 ||
        elf_header->e_shoff > (size_t) elf_size - sizeof(Elf64_Shdr)) {
      rs_trace("invalid e_shoff value in ELF header");
      return 0;
    }
    if (elf_header->e_shstrndx == SHN_UNDEF) {
      rs_trace("object file has no section name string table"
               " (e_shstrndx == SHN_UNDEF)");
      return 0;
    }
    /* Special case for more sections than will fit in e_shstrndx. */
    if (elf_header->e_shstrndx == SHN_XINDEX) {
      string_section = sections + sections[0].sh_link;
    }
    num_sections = elf_header->e_shnum;
    if (num_sections == 0) {
      /* Special case for more sections than will fit in e_shnum. */
      num_sections = sections[0].sh_size;
    }
    for (i = 0; i < num_sections; ++i) {
      const char *section_name = (char*)(elf_base +
                                         string_section->sh_offset +
                                         sections[i].sh_name);
      if (!strcmp(section_name, desired_section_name)) {
        desired_section = &sections[i];
        break;
      }
    }
    if (desired_section != NULL && desired_section->sh_size > 0) {
      int desired_section_size = desired_section->sh_size;
      *section_start = elf_base + desired_section->sh_offset;
      *section_size = desired_section_size;
      return 1;
    } else {
      return 0;
    }
  } else {
    rs_trace("unknown ELF class - neither ELFCLASS32 nor ELFCLASS64");
    return 0;
  }
}

/*
 * Search in a memory buffer (starting at @p base and of size @p size)
 * for a string (@p search), and replace @p search with @p replace
 * in all null-terminated strings that contain @p search.
 */
static int replace_string(void *base, size_t size,
                           const char *search, const char *replace) {
  char *start = (char *) base;
  char *end = (char *) base + size;
  int count = 0;
  char *p;
  size_t search_len = strlen(search);
  size_t replace_len = strlen(replace);

  assert(replace_len == search_len);

  if (size < search_len + 1)
    return 0;
  for (p = start; p < end - search_len - 1; p++) {
    if (memcmp(p, search, search_len) == 0) {
      memcpy(p, replace, replace_len);
      count++;
    }
  }
  return count;
}

/*
 * Map the specified file into memory with MAP_SHARED.
 * Returns the mapped address, and stores the file descriptor in @p p_fd.
 * It also fstats the file and stores the results in @p st.
 * Logs an error message and returns NULL on failure.
 */
static void *mmap_file(const char *path, int *p_fd, struct stat *st) {
  int fd;
  void *base;

  fd = open(path, O_RDWR);
  if (fd < 0) {
    rs_log_error("error opening file '%s': %s", path, strerror(errno));
    return NULL;
  }

  if (fstat(fd, st) != 0) {
    rs_log_error("fstat of file '%s' failed: %s", path, strerror(errno));
    close(fd);
    return NULL;
  }

  if (st->st_size <= 0) {
    rs_log_error("file '%s' has invalid file type or size", path);
    close(fd);
    return NULL;
  }

#ifdef HAVE_SYS_MMAP_H
  base = mmap(NULL, st->st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (base == MAP_FAILED) {
    rs_log_error("mmap of file '%s' failed: %s", path, strerror(errno));
    close(fd);
    return NULL;
  }
#else
  base = malloc(st->st_size);
  if (base == NULL) {
    rs_log_error("can't allocate buffer for %s: malloc failed", path);
    close(fd);
    return NULL;
  }
  errno = 0;
  if (read(fd, base, st->st_size) != st->st_size) {
    rs_log_error("can't read %ld bytes from %s: %s", (long) st->st_size, path,
                 strerror(errno));
    close(fd);
    return NULL;
  }
#endif
  *p_fd = fd;
  return base;
}

static int munmap_file(void *base, const char *path, int fd,
                const struct stat *st) {
  int status = 0;
#ifdef HAVE_SYS_MMAP_H
  if (munmap(base, st->st_size) != 0) {
    rs_log_error("munmap of file '%s' failed: %s", path, strerror(errno));
    status = 1;
  }
#else
  errno = 0;
  if (lseek(fd, 0, SEEK_SET) == -1) {
    rs_log_error("can't seek to start of %s: %s", path, strerror(errno));
    status = 1;
  } else if (write(fd, base, st->st_size) != st->st_size) {
    rs_log_error("can't write %ld bytes to %s: %s", (long) st->st_size, path,
                 strerror(errno));
    status = 1;
  }
#endif
  if (close(fd) != 0) {
    rs_log_error("close of file '%s' failed: %s", path, strerror(errno));
    status = 1;
  }
  return status;
}

/*
 * Update the ELF file residing at @p path, replacing all occurrences
 * of @p search with @p replace in the section named @p desired_section_name.
 * The replacement string must be the same length or shorter than
 * the search string.
 */
static void update_section(const char *path,
                           const void *base,
                           off_t size,
                           const char *desired_section_name,
                           const char *search,
                           const char *replace) {
  const void *desired_section = NULL;
  int desired_section_size = 0;

  if (FindElfSection(base, size, desired_section_name,
                     &desired_section, &desired_section_size)
      && desired_section_size > 0) {
    /* The local variable below works around a bug in some versions
     * of gcc (4.2.1?), which issues an erroneous warning if
     * 'desired_section_rw' is replaced with '(void *) desired_section'
     * in the call below, causing compile errors with -Werror.
     */
    void *desired_section_rw = (void *) desired_section;
    int count = replace_string(desired_section_rw, desired_section_size,
                               search, replace);
    if (count == 0) {
      rs_trace("\"%s\" section of file %s has no occurrences of \"%s\"",
               desired_section_name, path, search);
    } else {
      rs_log_info("updated \"%s\" section of file \"%s\": "
                  "replaced %d occurrences of \"%s\" with \"%s\"",
                  desired_section_name, path, count, search, replace);
      if (count > 1) {
        rs_log_warning("only expected to replace one occurrence!");
      }
    }
  } else {
    rs_trace("file %s has no \"%s\" section", path, desired_section_name);
  }
}

/*
 * Update the ELF file residing at @p path, replacing all occurrences
 * of @p search with @p replace in that file's ".debug_info" or
 * ".debug_str" section.
 * The replacement string must be the same length or shorter than
 * the search string.
 * Returns 0 on success (whether or not ".debug_info" section was
 * found or updated).
 * Returns 1 on serious error that should cause distcc to fail.
 */
static int update_debug_info(const char *path, const char *search,
                              const char *replace) {
  struct stat st;
  int fd;
  void *base;

  base = mmap_file(path, &fd, &st);
  if (base == NULL) {
    return 0;
  }

  update_section(path, base, st.st_size, ".debug_info", search, replace);
  update_section(path, base, st.st_size, ".debug_str", search, replace);

  return munmap_file(base, path, fd, &st);
}
#endif /* HAVE_ELF_H */

/*
 * Edit the ELF file residing at @p path, changing all occurrences of
 * the path @p server_path to @p client_path in the debugging info.
 *
 * We're a bit sloppy about that; rather than properly parsing
 * the DWARF debug info, finding the DW_AT_comp_dir (compilation working
 * directory) field and the DW_AT_name (source file name) field,
 * we just do a search-and-replace in the ".debug_info" and ".debug_str"
 * sections.  But this is good enough.
 *
 * Returns 0 on success (whether or not the ".debug_info" and ".debug_str"
 * sections were found or updated).
 * Returns 1 on serious error that should cause distcc to fail.
 */
int dcc_fix_debug_info(const char *path, const char *client_path,
                              const char *server_path)
{
#ifndef HAVE_ELF_H
  rs_trace("no <elf.h>, so can't change %s to %s in debug info for %s",
           server_path, client_path, path);
  return 0;
#else
  /*
   * We can only safely replace a string with another of exactly
   * the same length.  (Replacing a string with a shorter string
   * results in errors from gdb.)
   * So we append trailing slashes on the client side path.
   */
  size_t client_path_len = strlen(client_path);
  size_t server_path_len = strlen(server_path);
  assert(client_path_len <= server_path_len);
  char *client_path_plus_slashes = malloc(server_path_len + 1);
  if (!client_path_plus_slashes) {
    rs_log_crit("failed to allocate memory");
    return 1;
  }
  strcpy(client_path_plus_slashes, client_path);
  while (client_path_len < server_path_len) {
    client_path_plus_slashes[client_path_len++] = '/';
  }
  client_path_plus_slashes[client_path_len] = '\0';
  rs_log_info("client_path_plus_slashes = %s", client_path_plus_slashes);
  return update_debug_info(path, server_path, client_path_plus_slashes);
#endif
}

#ifdef TEST
const char *rs_program_name;

int main(int argc, char **argv) {
  rs_program_name = argv[0];
  rs_add_logger(rs_logger_file, RS_LOG_DEBUG, NULL, STDERR_FILENO);
  rs_trace_set_level(RS_LOG_DEBUG);
  if (argc != 4) {
    rs_log_error("Usage: %s <filename> <client-path> <server-path>",
                 rs_program_name);
    exit(1);
  }
  return dcc_fix_debug_info(argv[1], argv[2], argv[3]);
}
#endif
