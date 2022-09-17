#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdbool.h>
#include <memory.h>
#include <sys/mman.h>
#include <assert.h>

#include "light_elf.h"
#include "light_byteswap.h"

static char *fname;
static Elf64_Ehdr ehdr;

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define ELFDATANATIVE ELFDATA2LSB
#elif __BYTE_ORDER == __BIG_ENDIAN
#define ELFDATANATIVE ELFDATA2MSB
#else
#error "Unknown machine endian"
#endif

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
	size_t n = fread(buffer, length, sizeof(char), f);
        assert (n > 0);

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
