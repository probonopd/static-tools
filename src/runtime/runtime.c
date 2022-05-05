/**************************************************************************
 *
 * Copyright (c) 2004-22 Simon Peter
 * Portions Copyright (c) 2007 Alexander Larsson
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **************************************************************************/

#ident "AppImage by Simon Peter, https://appimage.org/"

#define _GNU_SOURCE

#include <stddef.h>

#include <squashfuse/ll.h>
#include <squashfuse/fuseprivate.h>
#include <squashfuse/nonstd.h>

extern dev_t sqfs_makedev(int maj, int min);

extern int sqfs_opt_proc(void *data, const char *arg, int key,
	struct fuse_args *outargs);

#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/wait.h>
#include <fnmatch.h>
#include <sys/mman.h>

typedef uint16_t Elf32_Half;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf32_Word;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;
typedef uint32_t Elf32_Addr;
typedef uint64_t Elf64_Addr;
typedef uint32_t Elf32_Off;
typedef uint64_t Elf64_Off;

#define EI_NIDENT 16

typedef struct elf32_hdr {
    unsigned char e_ident[EI_NIDENT];
    Elf32_Half e_type;
    Elf32_Half e_machine;
    Elf32_Word e_version;
    Elf32_Addr e_entry; /* Entry point */
    Elf32_Off e_phoff;
    Elf32_Off e_shoff;
    Elf32_Word e_flags;
    Elf32_Half e_ehsize;
    Elf32_Half e_phentsize;
    Elf32_Half e_phnum;
    Elf32_Half e_shentsize;
    Elf32_Half e_shnum;
    Elf32_Half e_shstrndx;
} Elf32_Ehdr;

typedef struct elf64_hdr {
    unsigned char e_ident[EI_NIDENT]; /* ELF "magic number" */
    Elf64_Half e_type;
    Elf64_Half e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry; /* Entry point virtual address */
    Elf64_Off e_phoff; /* Program header table file offset */
    Elf64_Off e_shoff; /* Section header table file offset */
    Elf64_Word e_flags;
    Elf64_Half e_ehsize;
    Elf64_Half e_phentsize;
    Elf64_Half e_phnum;
    Elf64_Half e_shentsize;
    Elf64_Half e_shnum;
    Elf64_Half e_shstrndx;
} Elf64_Ehdr;

typedef struct elf32_shdr {
    Elf32_Word sh_name;
    Elf32_Word sh_type;
    Elf32_Word sh_flags;
    Elf32_Addr sh_addr;
    Elf32_Off sh_offset;
    Elf32_Word sh_size;
    Elf32_Word sh_link;
    Elf32_Word sh_info;
    Elf32_Word sh_addralign;
    Elf32_Word sh_entsize;
} Elf32_Shdr;

typedef struct elf64_shdr {
    Elf64_Word sh_name; /* Section name, index in string tbl */
    Elf64_Word sh_type; /* Type of section */
    Elf64_Xword sh_flags; /* Miscellaneous section attributes */
    Elf64_Addr sh_addr; /* Section virtual addr at execution */
    Elf64_Off sh_offset; /* Section file offset */
    Elf64_Xword sh_size; /* Size of section in bytes */
    Elf64_Word sh_link; /* Index of another section */
    Elf64_Word sh_info; /* Additional section information */
    Elf64_Xword sh_addralign; /* Section alignment */
    Elf64_Xword sh_entsize; /* Entry size if section holds table */
} Elf64_Shdr;

/* Note header in a PT_NOTE section */
typedef struct elf32_note {
    Elf32_Word n_namesz; /* Name size */
    Elf32_Word n_descsz; /* Content size */
    Elf32_Word n_type; /* Content type */
} Elf32_Nhdr;

#define ELFCLASS32  1
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2
#define ELFCLASS64  2
#define EI_CLASS    4
#define EI_DATA     5

#define bswap_16(value) \
((((value) & 0xff) << 8) | ((value) >> 8))

#define bswap_32(value) \
(((uint32_t)bswap_16((uint16_t)((value) & 0xffff)) << 16) | \
(uint32_t)bswap_16((uint16_t)((value) >> 16)))

#define bswap_64(value) \
(((uint64_t)bswap_32((uint32_t)((value) & 0xffffffff)) \
<< 32) | \
(uint64_t)bswap_32((uint32_t)((value) >> 32)))

typedef Elf32_Nhdr Elf_Nhdr;

static char *fname;
static Elf64_Ehdr ehdr;

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define ELFDATANATIVE ELFDATA2LSB
#elif __BYTE_ORDER == __BIG_ENDIAN
#define ELFDATANATIVE ELFDATA2MSB
#else
#error "Unknown machine endian"
#endif

static uint16_t file16_to_cpu(uint16_t val)
{
    if (ehdr.e_ident[EI_DATA] != ELFDATANATIVE)
        val = bswap_16(val);
    return val;
}

static uint32_t file32_to_cpu(uint32_t val)
{
    if (ehdr.e_ident[EI_DATA] != ELFDATANATIVE)
        val = bswap_32(val);
    return val;
}

static uint64_t file64_to_cpu(uint64_t val)
{
    if (ehdr.e_ident[EI_DATA] != ELFDATANATIVE)
        val = bswap_64(val);
    return val;
}

static off_t read_elf32(FILE* fd)
{
    Elf32_Ehdr ehdr32;
    Elf32_Shdr shdr32;
    off_t last_shdr_offset;
    ssize_t ret;
    off_t  sht_end, last_section_end;

    fseeko(fd, 0, SEEK_SET);
    ret = fread(&ehdr32, 1, sizeof(ehdr32), fd);
    if (ret < 0 || (size_t)ret != sizeof(ehdr32)) {
        fprintf(stderr, "Read of ELF header from %s failed: %s\n",
            fname, strerror(errno));
        return -1;
    }

    ehdr.e_shoff		= file32_to_cpu(ehdr32.e_shoff);
    ehdr.e_shentsize	= file16_to_cpu(ehdr32.e_shentsize);
    ehdr.e_shnum		= file16_to_cpu(ehdr32.e_shnum);

    last_shdr_offset = ehdr.e_shoff + (ehdr.e_shentsize * (ehdr.e_shnum - 1));
    fseeko(fd, last_shdr_offset, SEEK_SET);
    ret = fread(&shdr32, 1, sizeof(shdr32), fd);
    if (ret < 0 || (size_t)ret != sizeof(shdr32)) {
        fprintf(stderr, "Read of ELF section header from %s failed: %s\n",
            fname, strerror(errno));
        return -1;
    }

    /* ELF ends either with the table of section headers (SHT) or with a section. */
    sht_end = ehdr.e_shoff + (ehdr.e_shentsize * ehdr.e_shnum);
    last_section_end = file64_to_cpu(shdr32.sh_offset) + file64_to_cpu(shdr32.sh_size);
    return sht_end > last_section_end ? sht_end : last_section_end;
}

static off_t read_elf64(FILE* fd)
{
    Elf64_Ehdr ehdr64;
    Elf64_Shdr shdr64;
    off_t last_shdr_offset;
    off_t ret;
    off_t sht_end, last_section_end;

    fseeko(fd, 0, SEEK_SET);
    ret = fread(&ehdr64, 1, sizeof(ehdr64), fd);
    if (ret < 0 || (size_t)ret != sizeof(ehdr64)) {
        fprintf(stderr, "Read of ELF header from %s failed: %s\n",
            fname, strerror(errno));
        return -1;
    }

    ehdr.e_shoff		= file64_to_cpu(ehdr64.e_shoff);
    ehdr.e_shentsize	= file16_to_cpu(ehdr64.e_shentsize);
    ehdr.e_shnum		= file16_to_cpu(ehdr64.e_shnum);

    last_shdr_offset = ehdr.e_shoff + (ehdr.e_shentsize * (ehdr.e_shnum - 1));
    fseeko(fd, last_shdr_offset, SEEK_SET);
    ret = fread(&shdr64, 1, sizeof(shdr64), fd);
    if (ret < 0 || ret != sizeof(shdr64)) {
        fprintf(stderr, "Read of ELF section header from %s failed: %s\n",
            fname, strerror(errno));
        return -1;
    }

    /* ELF ends either with the table of section headers (SHT) or with a section. */
    sht_end = ehdr.e_shoff + (ehdr.e_shentsize * ehdr.e_shnum);
    last_section_end = file64_to_cpu(shdr64.sh_offset) + file64_to_cpu(shdr64.sh_size);
    return sht_end > last_section_end ? sht_end : last_section_end;
}

ssize_t appimage_get_elf_size(const char* fname) {
    off_t ret;
    FILE* fd = NULL;
    off_t size = -1;

    fd = fopen(fname, "rb");
    if (fd == NULL) {
        fprintf(stderr, "Cannot open %s: %s\n",
            fname, strerror(errno));
        return -1;
    }
    ret = fread(ehdr.e_ident, 1, EI_NIDENT, fd);
    if (ret != EI_NIDENT) {
        fprintf(stderr, "Read of e_ident from %s failed: %s\n",
            fname, strerror(errno));
        return -1;
    }
    if ((ehdr.e_ident[EI_DATA] != ELFDATA2LSB) &&
        (ehdr.e_ident[EI_DATA] != ELFDATA2MSB)) {
        fprintf(stderr, "Unknown ELF data order %u\n",
            ehdr.e_ident[EI_DATA]);
        return -1;
    }
    if (ehdr.e_ident[EI_CLASS] == ELFCLASS32) {
        size = read_elf32(fd);
    } else if (ehdr.e_ident[EI_CLASS] == ELFCLASS64) {
        size = read_elf64(fd);
    } else {
        fprintf(stderr, "Unknown ELF class %u\n", ehdr.e_ident[EI_CLASS]);
        return -1;
    }

    fclose(fd);
    return size;
}

/* Return the offset, and the length of an ELF section with a given name in a given ELF file */
bool appimage_get_elf_section_offset_and_length(const char* fname, const char* section_name, unsigned long* offset, unsigned long* length) {
    uint8_t* data;
    int i;
    int fd = open(fname, O_RDONLY);
    size_t map_size = (size_t) lseek(fd, 0, SEEK_END);

    data = mmap(NULL, map_size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);

    // this trick works as both 32 and 64 bit ELF files start with the e_ident[EI_NINDENT] section
    unsigned char class = data[EI_CLASS];

    if (class == ELFCLASS32) {
        Elf32_Ehdr* elf;
        Elf32_Shdr* shdr;

        elf = (Elf32_Ehdr*) data;
        shdr = (Elf32_Shdr*) (data + ((Elf32_Ehdr*) elf)->e_shoff);

        char* strTab = (char*) (data + shdr[elf->e_shstrndx].sh_offset);
        for (i = 0; i < elf->e_shnum; i++) {
            if (strcmp(&strTab[shdr[i].sh_name], section_name) == 0) {
                *offset = shdr[i].sh_offset;
                *length = shdr[i].sh_size;
            }
        }
    } else if (class == ELFCLASS64) {
        Elf64_Ehdr* elf;
        Elf64_Shdr* shdr;

        elf = (Elf64_Ehdr*) data;
        shdr = (Elf64_Shdr*) (data + elf->e_shoff);

        char* strTab = (char*) (data + shdr[elf->e_shstrndx].sh_offset);
        for (i = 0; i < elf->e_shnum; i++) {
            if (strcmp(&strTab[shdr[i].sh_name], section_name) == 0) {
                *offset = shdr[i].sh_offset;
                *length = shdr[i].sh_size;
            }
        }
    } else {
        fprintf(stderr, "Platforms other than 32-bit/64-bit are currently not supported!");
        munmap(data, map_size);
        return false;
    }

    munmap(data, map_size);
    return true;
}

char* read_file_offset_length(const char* fname, unsigned long offset, unsigned long length) {
    FILE* f;
    if ((f = fopen(fname, "r")) == NULL) {
        return NULL;
    }

    fseek(f, offset, SEEK_SET);

    char* buffer = calloc(length + 1, sizeof(char));
    fread(buffer, length, sizeof(char), f);

    fclose(f);

    return buffer;
}

int appimage_print_hex(char* fname, unsigned long offset, unsigned long length) {
    char* data;
    if ((data = read_file_offset_length(fname, offset, length)) == NULL) {
        return 1;
    }

    for (long long k = 0; k < length && data[k] != '\0'; k++) {
        printf("%x", data[k]);
    }

    free(data);

    printf("\n");

    return 0;
}

int appimage_print_binary(char* fname, unsigned long offset, unsigned long length) {
    char* data;
    if ((data = read_file_offset_length(fname, offset, length)) == NULL) {
        return 1;
    }

    printf("%s\n", data);

    free(data);

    return 0;
}


/* Exit status to use when launching an AppImage fails.
 * For applications that assign meanings to exit status codes (e.g. rsync),
 * we avoid "cluttering" pre-defined exit status codes by using 127 which
 * is known to alias an application exit status and also known as launcher
 * error, see SYSTEM(3POSIX).
 */
#define EXIT_EXECERROR  127     /* Execution error exit status.  */

struct stat st;

static ssize_t fs_offset; // The offset at which a filesystem image is expected = end of this ELF

static void die(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_EXECERROR);
}

/* Check whether directory is writable */
bool is_writable_directory(char* str) {
    if(access(str, W_OK) == 0) {
        return true;
    } else {
        return false;
    }
}

bool startsWith(const char *pre, const char *str)
{
    size_t lenpre = strlen(pre),
    lenstr = strlen(str);
    return lenstr < lenpre ? false : strncmp(pre, str, lenpre) == 0;
}

/* Fill in a stat structure. Does not set st_ino */
sqfs_err private_sqfs_stat(sqfs *fs, sqfs_inode *inode, struct stat *st) {
        sqfs_err err = SQFS_OK;
        uid_t id;

        memset(st, 0, sizeof(*st));
        st->st_mode = inode->base.mode;
        st->st_nlink = inode->nlink;
        st->st_mtime = st->st_ctime = st->st_atime = inode->base.mtime;

        if (S_ISREG(st->st_mode)) {
                /* FIXME: do symlinks, dirs, etc have a size? */
                st->st_size = inode->xtra.reg.file_size;
                st->st_blocks = st->st_size / 512;
        } else if (S_ISBLK(st->st_mode) || S_ISCHR(st->st_mode)) {
                st->st_rdev = sqfs_makedev(inode->xtra.dev.major,
                        inode->xtra.dev.minor);
        } else if (S_ISLNK(st->st_mode)) {
                st->st_size = inode->xtra.symlink_size;
        }

        st->st_blksize = fs->sb.block_size; /* seriously? */

        err = sqfs_id_get(fs, inode->base.uid, &id);
        if (err)
                return err;
        st->st_uid = id;
        err = sqfs_id_get(fs, inode->base.guid, &id);
        st->st_gid = id;
        if (err)
                return err;

        return SQFS_OK;
}

/* ================= End ELF parsing */

extern int fusefs_main(int argc, char *argv[], void (*mounted) (void));
// extern void ext2_quit(void);

static pid_t fuse_pid;
static int keepalive_pipe[2];

static void *
write_pipe_thread (void *arg)
{
    char c[32];
    int res;
    //  sprintf(stderr, "Called write_pipe_thread");
    memset (c, 'x', sizeof (c));
    while (1) {
        /* Write until we block, on broken pipe, exit */
        res = write (keepalive_pipe[1], c, sizeof (c));
        if (res == -1) {
            kill (fuse_pid, SIGTERM);
            break;
        }
    }
    return NULL;
}

void
fuse_mounted (void)
{
    pthread_t thread;
    fuse_pid = getpid();
    pthread_create(&thread, NULL, write_pipe_thread, keepalive_pipe);
}

char* getArg(int argc, char *argv[],char chr)
{
    int i;
    for (i=1; i<argc; ++i)
        if ((argv[i][0]=='-') && (argv[i][1]==chr))
            return &(argv[i][2]);
    return NULL;
}

/* mkdir -p implemented in C, needed for https://github.com/AppImage/AppImageKit/issues/333
 * https://gist.github.com/JonathonReinhart/8c0d90191c38af2dcadb102c4e202950 */
int
mkdir_p(const char* const path)
{
    /* Adapted from http://stackoverflow.com/a/2336245/119527 */
    const size_t len = strlen(path);
    char _path[PATH_MAX];
    char *p;

    errno = 0;

    /* Copy string so its mutable */
    if (len > sizeof(_path)-1) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(_path, path);

    /* Iterate the string */
    for (p = _path + 1; *p; p++) {
        if (*p == '/') {
            /* Temporarily truncate */
            *p = '\0';

            if (mkdir(_path, 0755) != 0) {
                if (errno != EEXIST)
                    return -1;
            }

            *p = '/';
        }
    }

    if (mkdir(_path, 0755) != 0) {
        if (errno != EEXIST)
            return -1;
    }

    return 0;
}

void print_help(const char *appimage_path)
{
    // TODO: "--appimage-list                 List content from embedded filesystem image\n"
    fprintf(stderr,
        "AppImage options:\n\n"
        "  --appimage-extract [<pattern>]  Extract content from embedded filesystem image\n"
        "                                  If pattern is passed, only extract matching files\n"
        "  --appimage-help                 Print this help\n"
        "  --appimage-mount                Mount embedded filesystem image and print\n"
        "                                  mount point and wait for kill with Ctrl-C\n"
        "  --appimage-offset               Print byte offset to start of embedded\n"
        "                                  filesystem image\n"
        "  --appimage-portable-home        Create a portable home folder to use as $HOME\n"
        "  --appimage-portable-config      Create a portable config folder to use as\n"
        "                                  $XDG_CONFIG_HOME\n"
        "  --appimage-signature            Print digital signature embedded in AppImage\n"
        "  --appimage-updateinfo[rmation]  Print update info embedded in AppImage\n"
        "  --appimage-version              Print version of AppImageKit\n"
        "\n"
        "Portable home:\n"
        "\n"
        "  If you would like the application contained inside this AppImage to store its\n"
        "  data alongside this AppImage rather than in your home directory, then you can\n"
        "  place a directory named\n"
        "\n"
        "  %s.home\n"
        "\n"
        "  Or you can invoke this AppImage with the --appimage-portable-home option,\n"
        "  which will create this directory for you. As long as the directory exists\n"
        "  and is neither moved nor renamed, the application contained inside this\n"
        "  AppImage to store its data in this directory rather than in your home\n"
        "  directory\n"
    , appimage_path);
}

void portable_option(const char *arg, const char *appimage_path, const char *name)
{
    char option[32];
    sprintf(option, "appimage-portable-%s", name);

    if (arg && strcmp(arg, option)==0) {
        char portable_dir[PATH_MAX];
        char fullpath[PATH_MAX];

        ssize_t length = readlink(appimage_path, fullpath, sizeof(fullpath));
        if (length < 0) {
            fprintf(stderr, "Error getting realpath for %s\n", appimage_path);
            exit(EXIT_FAILURE);
        }
        fullpath[length] = '\0';

        sprintf(portable_dir, "%s.%s", fullpath, name);
        if (!mkdir(portable_dir, S_IRWXU))
            fprintf(stderr, "Portable %s directory created at %s\n", name, portable_dir);
        else
            fprintf(stderr, "Error creating portable %s directory at %s: %s\n", name, portable_dir, strerror(errno));

        exit(0);
    }
}

bool extract_appimage(const char* const appimage_path, const char* const _prefix, const char* const _pattern, const bool overwrite, const bool verbose) {
    sqfs_err err = SQFS_OK;
    sqfs_traverse trv;
    sqfs fs;
    char prefixed_path_to_extract[1024];

    // local copy we can modify safely
    // allocate 1 more byte than we would need so we can add a trailing slash if there is none yet
    char* prefix = malloc(strlen(_prefix) + 2);
    strcpy(prefix, _prefix);

    // sanitize prefix
    if (prefix[strlen(prefix) - 1] != '/')
        strcat(prefix, "/");

    if (access(prefix, F_OK) == -1) {
        if (mkdir_p(prefix) == -1) {
            perror("mkdir_p error");
            return false;
        }
    }

    if ((err = sqfs_open_image(&fs, appimage_path, (size_t) fs_offset))) {
        fprintf(stderr, "Failed to open squashfs image\n");
        return false;
    };

    // track duplicate inodes for hardlinks
    char** created_inode = calloc(fs.sb.inodes, sizeof(char*));
    if (created_inode == NULL) {
        fprintf(stderr, "Failed allocating memory to track hardlinks\n");
        return false;
    }

    if ((err = sqfs_traverse_open(&trv, &fs, sqfs_inode_root(&fs)))) {
        fprintf(stderr, "sqfs_traverse_open error\n");
        free(created_inode);
        return false;
    }

    bool rv = true;

    while (sqfs_traverse_next(&trv, &err)) {
        if (!trv.dir_end) {
            if (_pattern == NULL || fnmatch(_pattern, trv.path, FNM_FILE_NAME | FNM_LEADING_DIR) == 0) {
                // fprintf(stderr, "trv.path: %s\n", trv.path);
                // fprintf(stderr, "sqfs_inode_id: %lu\n", trv.entry.inode);
                sqfs_inode inode;
                if (sqfs_inode_get(&fs, &inode, trv.entry.inode)) {
                    fprintf(stderr, "sqfs_inode_get error\n");
                    rv = false;
                    break;
                }
                // fprintf(stderr, "inode.base.inode_type: %i\n", inode.base.inode_type);
                // fprintf(stderr, "inode.xtra.reg.file_size: %lu\n", inode.xtra.reg.file_size);
                strcpy(prefixed_path_to_extract, "");
                strcat(strcat(prefixed_path_to_extract, prefix), trv.path);

                if (verbose)
                    fprintf(stdout, "%s\n", prefixed_path_to_extract);

                if (inode.base.inode_type == SQUASHFS_DIR_TYPE || inode.base.inode_type == SQUASHFS_LDIR_TYPE) {
                    // fprintf(stderr, "inode.xtra.dir.parent_inode: %ui\n", inode.xtra.dir.parent_inode);
                    // fprintf(stderr, "mkdir_p: %s/\n", prefixed_path_to_extract);
                    if (access(prefixed_path_to_extract, F_OK) == -1) {
                        if (mkdir_p(prefixed_path_to_extract) == -1) {
                            perror("mkdir_p error");
                            rv = false;
                            break;
                        }
                    }
                } else if (inode.base.inode_type == SQUASHFS_REG_TYPE || inode.base.inode_type == SQUASHFS_LREG_TYPE) {
                    // if we've already created this inode, then this is a hardlink
                    char* existing_path_for_inode = created_inode[inode.base.inode_number - 1];
                    if (existing_path_for_inode != NULL) {
                        unlink(prefixed_path_to_extract);
                        if (link(existing_path_for_inode, prefixed_path_to_extract) == -1) {
                            fprintf(stderr, "Couldn't create hardlink from \"%s\" to \"%s\": %s\n",
                                prefixed_path_to_extract, existing_path_for_inode, strerror(errno));
                            rv = false;
                            break;
                        } else {
                            continue;
                        }
                    } else {
                        struct stat st;
                        if (!overwrite && stat(prefixed_path_to_extract, &st) == 0 && st.st_size == inode.xtra.reg.file_size) {
                            fprintf(stderr, "File exists and file size matches, skipping\n");
                            continue;
                        }

                        // track the path we extract to for this inode, so that we can `link` if this inode is found again
                        created_inode[inode.base.inode_number - 1] = strdup(prefixed_path_to_extract);
                        // fprintf(stderr, "Extract to: %s\n", prefixed_path_to_extract);
                        if (private_sqfs_stat(&fs, &inode, &st) != 0)
                            die("private_sqfs_stat error");

                        // create parent dir
                        char* p = strrchr(prefixed_path_to_extract, '/');
                        if (p) {
                            // set an \0 to end the split the string
                            *p = '\0';
                            mkdir_p(prefixed_path_to_extract);

                            // restore dir seprator
                            *p = '/';
                        }

                        // Read the file in chunks
                        off_t bytes_already_read = 0;
                        sqfs_off_t bytes_at_a_time = 64 * 1024;
                        FILE* f;
                        f = fopen(prefixed_path_to_extract, "w+");
                        if (f == NULL) {
                            perror("fopen error");
                            rv = false;
                            break;
                        }
                        while (bytes_already_read < inode.xtra.reg.file_size) {
                            char buf[bytes_at_a_time];
                            if (sqfs_read_range(&fs, &inode, (sqfs_off_t) bytes_already_read, &bytes_at_a_time, buf)) {
                                perror("sqfs_read_range error");
                                rv = false;
                                break;
                            }
                            // fwrite(buf, 1, bytes_at_a_time, stdout);
                            fwrite(buf, 1, bytes_at_a_time, f);
                            bytes_already_read = bytes_already_read + bytes_at_a_time;
                        }
                        fclose(f);
                        chmod(prefixed_path_to_extract, st.st_mode);
                        if (!rv)
                            break;
                    }
                } else if (inode.base.inode_type == SQUASHFS_SYMLINK_TYPE || inode.base.inode_type == SQUASHFS_LSYMLINK_TYPE) {
                    size_t size;
                    sqfs_readlink(&fs, &inode, NULL, &size);
                    char buf[size];
                    int ret = sqfs_readlink(&fs, &inode, buf, &size);
                    if (ret != 0) {
                        perror("symlink error");
                        rv = false;
                        break;
                    }
                    // fprintf(stderr, "Symlink: %s to %s \n", prefixed_path_to_extract, buf);
                    unlink(prefixed_path_to_extract);
                    ret = symlink(buf, prefixed_path_to_extract);
                    if (ret != 0)
                        fprintf(stderr, "WARNING: could not create symlink\n");
                } else {
                    fprintf(stderr, "TODO: Implement inode.base.inode_type %i\n", inode.base.inode_type);
                }
                // fprintf(stderr, "\n");

                if (!rv)
                    break;
            }
        }
    }
    for (int i = 0; i < fs.sb.inodes; i++) {
        free(created_inode[i]);
    }
    free(created_inode);

    if (err != SQFS_OK) {
        fprintf(stderr, "sqfs_traverse_next error\n");
        rv = false;
    }
    sqfs_traverse_close(&trv);
    sqfs_fd_close(fs.fd);

    return rv;
}

void build_mount_point(char* mount_dir, const char* const argv0, char const* const temp_base, const size_t templen) {
    const size_t maxnamelen = 6;

    char* path_basename;
    path_basename = basename(argv0);

    size_t namelen = strlen(path_basename);
    // limit length of tempdir name
    if (namelen > maxnamelen) {
        namelen = maxnamelen;
    }

    strcpy(mount_dir, temp_base);
    strncpy(mount_dir + templen, "/.mount_", 8);
    strncpy(mount_dir + templen + 8, path_basename, namelen);
    strncpy(mount_dir+templen+8+namelen, "XXXXXX", 6);
    mount_dir[templen+8+namelen+6] = 0; // null terminate destination
}

int fusefs_main(int argc, char *argv[], void (*mounted) (void)) {
    struct fuse_args args;
    sqfs_opts opts;

#if FUSE_USE_VERSION >= 30
    struct fuse_cmdline_opts fuse_cmdline_opts;
#else
    struct {
        char *mountpoint;
        int mt, foreground;
    } fuse_cmdline_opts;
#endif

    int err;
    sqfs_ll *ll;
    struct fuse_opt fuse_opts[] = {
        {"offset=%zu", offsetof(sqfs_opts, offset), 0},
        {"timeout=%u", offsetof(sqfs_opts, idle_timeout_secs), 0},
        FUSE_OPT_END
    };
    
    struct fuse_lowlevel_ops sqfs_ll_ops;
    memset(&sqfs_ll_ops, 0, sizeof(sqfs_ll_ops));
    sqfs_ll_ops.getattr		= sqfs_ll_op_getattr;
    sqfs_ll_ops.opendir		= sqfs_ll_op_opendir;
    sqfs_ll_ops.releasedir	= sqfs_ll_op_releasedir;
    sqfs_ll_ops.readdir		= sqfs_ll_op_readdir;
    sqfs_ll_ops.lookup		= sqfs_ll_op_lookup;
    sqfs_ll_ops.open		= sqfs_ll_op_open;
    sqfs_ll_ops.create		= sqfs_ll_op_create;
    sqfs_ll_ops.release		= sqfs_ll_op_release;
    sqfs_ll_ops.read		= sqfs_ll_op_read;
    sqfs_ll_ops.readlink	= sqfs_ll_op_readlink;
    sqfs_ll_ops.listxattr	= sqfs_ll_op_listxattr;
    sqfs_ll_ops.getxattr	= sqfs_ll_op_getxattr;
    sqfs_ll_ops.forget		= sqfs_ll_op_forget;
    sqfs_ll_ops.statfs    = stfs_ll_op_statfs;
   
    /* PARSE ARGS */
    args.argc = argc;
    args.argv = argv;
    args.allocated = 0;
    
    opts.progname = argv[0];
    opts.image = NULL;
    opts.mountpoint = 0;
    opts.offset = 0;
    opts.idle_timeout_secs = 0;
    if (fuse_opt_parse(&args, &opts, fuse_opts, sqfs_opt_proc) == -1)
        sqfs_usage(argv[0], true);

#if FUSE_USE_VERSION >= 30
	if (fuse_parse_cmdline(&args, &fuse_cmdline_opts) != 0)
#else
	if (fuse_parse_cmdline(&args,
                           &fuse_cmdline_opts.mountpoint,
                           &fuse_cmdline_opts.mt,
                           &fuse_cmdline_opts.foreground) == -1)
#endif
        sqfs_usage(argv[0], true);
    if (fuse_cmdline_opts.mountpoint == NULL)
        sqfs_usage(argv[0], true);

    /* fuse_daemonize() will unconditionally clobber fds 0-2.
     *
     * If we get one of these file descriptors in sqfs_ll_open,
     * we're going to have a bad time. Just make sure that all
     * these fds are open before opening the image file, that way
     * we must get a different fd.
     */
    while (true) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd == -1) {
        /* Can't open /dev/null, how bizarre! However,
         * fuse_deamonize won't clobber fds if it can't
         * open /dev/null either, so we ought to be OK.
         */
        break;
        }
        if (fd > 2) {
        /* fds 0-2 are now guaranteed to be open. */
        close(fd);
        break;
        }
    }

    /* OPEN FS */
    err = !(ll = sqfs_ll_open(opts.image, opts.offset));
    
    /* STARTUP FUSE */
    if (!err) {
        sqfs_ll_chan ch;
        err = -1;
        if (sqfs_ll_mount(
                        &ch,
                        fuse_cmdline_opts.mountpoint,
                        &args,
                        &sqfs_ll_ops,
                        sizeof(sqfs_ll_ops),
                        ll) == SQFS_OK) {
            if (sqfs_ll_daemonize(fuse_cmdline_opts.foreground) != -1) {
                if (fuse_set_signal_handlers(ch.session) != -1) {
                    if (opts.idle_timeout_secs) {
                        setup_idle_timeout(ch.session, opts.idle_timeout_secs);
                    }
                    if (mounted)
                        mounted();
                    /* FIXME: multithreading */
                    err = fuse_session_loop(ch.session);
                    teardown_idle_timeout();
                    fuse_remove_signal_handlers(ch.session);
                }
            }
            sqfs_ll_destroy(ll);
            sqfs_ll_unmount(&ch, fuse_cmdline_opts.mountpoint);
        }
    }
    fuse_opt_free_args(&args);
    if (mounted)
        rmdir(fuse_cmdline_opts.mountpoint);
    free(ll);
    free(fuse_cmdline_opts.mountpoint);
    
    return -err;
}

int main(int argc, char *argv[]) {
    char appimage_path[PATH_MAX];
    char argv0_path[PATH_MAX];
    char * arg;

    /* We might want to operate on a target appimage rather than this file itself,
     * e.g., for appimaged which must not run untrusted code from random AppImages.
     * This variable is intended for use by e.g., appimaged and is subject to
     * change any time. Do not rely on it being present. We might even limit this
     * functionality specifically for builds used by appimaged.
     */
    if (getenv("TARGET_APPIMAGE") == NULL) {
        strcpy(appimage_path, "/proc/self/exe");
        strcpy(argv0_path, argv[0]);
    } else {
        strcpy(appimage_path, getenv("TARGET_APPIMAGE"));
        strcpy(argv0_path, getenv("TARGET_APPIMAGE"));
    }

    // temporary directories are required in a few places
    // therefore we implement the detection of the temp base dir at the top of the code to avoid redundancy
    char temp_base[PATH_MAX] = P_tmpdir;

    {
        const char* const TMPDIR = getenv("TMPDIR");
        if (TMPDIR != NULL)
            strcpy(temp_base, getenv("TMPDIR"));
    }

    fs_offset = appimage_get_elf_size(appimage_path);

    // error check
    if (fs_offset < 0) {
        fprintf(stderr, "Failed to get fs offset for %s\n", appimage_path);
        exit(EXIT_EXECERROR);
    }

    arg=getArg(argc,argv,'-');

    /* Just print the offset and then exit */
    if(arg && strcmp(arg,"appimage-offset")==0) {
        printf("%zu\n", fs_offset);
        exit(0);
    }

    arg=getArg(argc,argv,'-');

    /* extract the AppImage */
    if(arg && strcmp(arg,"appimage-extract")==0) {
        char* pattern;

        // default use case: use standard prefix
        if (argc == 2) {
            pattern = NULL;
        } else if (argc == 3) {
            pattern = argv[2];
        } else {
            fprintf(stderr, "Unexpected argument count: %d\n", argc - 1);
            fprintf(stderr, "Usage: %s --appimage-extract [<prefix>]\n", argv0_path);
            exit(1);
        }

        if (!extract_appimage(appimage_path, "squashfs-root/", pattern, true, true)) {
            exit(1);
        }

        exit(0);
    }

    // calculate full path of AppImage
    char fullpath[PATH_MAX];

    // If we are operating on this file itself
    ssize_t len = readlink(appimage_path, fullpath, sizeof(fullpath));
    if (len < 0) {
        perror("Failed to obtain absolute path");
        exit(EXIT_EXECERROR);
    }
    fullpath[len] = '\0';

    int dir_fd, res;

    size_t templen = strlen(temp_base);

    // allocate enough memory (size of name won't exceed 60 bytes)
    char mount_dir[templen + 60];

    build_mount_point(mount_dir, argv[0], temp_base, templen);

    size_t mount_dir_size = strlen(mount_dir);
    pid_t pid;
    char **real_argv;
    int i;

    if (mkdtemp(mount_dir) == NULL) {
        perror ("create mount dir error");
        exit (EXIT_EXECERROR);
    }

    if (pipe (keepalive_pipe) == -1) {
        perror ("pipe error");
        exit (EXIT_EXECERROR);
    }

    pid = fork ();
    if (pid == -1) {
        perror ("fork error");
        exit (EXIT_EXECERROR);
    }

    if (pid == 0) {
        /* in child */

        char *child_argv[5];

        /* close read pipe */
        close (keepalive_pipe[0]);

        char *dir = realpath(appimage_path, NULL );

        char options[100];
        sprintf(options, "ro,offset=%zu", fs_offset);

        child_argv[0] = dir;
        child_argv[1] = "-o";
        child_argv[2] = options;
        child_argv[3] = dir;
        child_argv[4] = mount_dir;

        if(0 != fusefs_main (5, child_argv, fuse_mounted)){
            char *title;
            char *body;
            title = "Cannot mount squashfs, make sure you have fusermount in your path.";
            body = "If everything fails, use --squashfs-extract to extract in the current directory";
            printf("\n%s\n", title);
            printf("%s\n", body);
        };
    } else {
        /* in parent, child is $pid */
        int c;

        /* close write pipe */
        close (keepalive_pipe[1]);

        /* Pause until mounted */
        read (keepalive_pipe[0], &c, 1);

        /* Fuse process has now daemonized, reap our child */
        waitpid(pid, NULL, 0);

        dir_fd = open (mount_dir, O_RDONLY);
        if (dir_fd == -1) {
            perror ("open dir error");
            exit (EXIT_EXECERROR);
        }

        res = dup2 (dir_fd, 1023);
        if (res == -1) {
            perror ("dup2 error");
            exit (EXIT_EXECERROR);
        }
        close (dir_fd);

        real_argv = malloc (sizeof (char *) * (argc + 1));
        for (i = 0; i < argc; i++) {
            real_argv[i] = argv[i];
        }
        real_argv[i] = NULL;

        if(arg && strcmp(arg, "appimage-mount") == 0) {
            char real_mount_dir[PATH_MAX];

            if (realpath(mount_dir, real_mount_dir) == real_mount_dir) {
                printf("%s\n", real_mount_dir);
            } else {
                printf("%s\n", mount_dir);
            }

            // stdout is, by default, buffered (unlike stderr), therefore in order to allow other processes to read
            // the path from stdout, we need to flush the buffers now
            // this is a less-invasive alternative to setbuf(stdout, NULL);
            fflush(stdout);

            for (;;) pause();

            exit(0);
        }

        /* Setting some environment variables that the app "inside" might use */
        setenv( "APPIMAGE", fullpath, 1 );
        setenv( "ARGV0", argv0_path, 1 );
        setenv( "APPDIR", mount_dir, 1 );

        /* Original working directory */
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            setenv( "OWD", cwd, 1 );
        }

        char filename[mount_dir_size + 8]; /* enough for mount_dir + "/AppRun" */
        strcpy (filename, mount_dir);
        strcat (filename, "/AppRun");

        /* TODO: Find a way to get the exit status and/or output of this */
        execv (filename, real_argv);
        /* Error if we continue here */
        perror("execv error");
        exit(EXIT_EXECERROR);
    }

    return 0;
}
