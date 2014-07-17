/* getsect.c
 *
 * well, lost previous code, had to do same thing twice, but with less feature this time
 *
 * TODO
 *  / clean 32b support
 *  - 64b ELF support
 *
 * eldre8 stupid old code
 */

#include <stdio.h>
#include <stdlib.h>
#include <elf.h>

#define ARG(x,y) do{ fprintf(stderr, x); exit(y); }while(0);

void usage(char *prog, int code) {
    fprintf(stderr, "usage: %s <elf_file> <section_name> <output_file>\n", prog);
    exit(code);
}

int main(int ac, char **av) {
    FILE *fpin, *fpout;
    //int ecode=0, i, soff, snum, strndx, stroff;
    int ecode=0, i;
    Elf64_Off soff, stroff;
    Elf64_Half snum, strndx;
    unsigned char *buffer, *bufname, *ptr;
    Elf64_Ehdr *elfe;
    Elf64_Shdr *elfs;

    if (ac<4) usage(av[0], 3);

    fpin=fopen(av[1], "r");
    if (!fpin) ARG("this file doesn't exist or whatever else...\n", 3);

    /* lecture du header principal */
    buffer=malloc(sizeof(Elf64_Ehdr));
    fread(buffer, sizeof(Elf64_Ehdr), 1, fpin);
    elfe=(Elf64_Ehdr*)buffer;
    soff=elfe->e_shoff;
    snum=elfe->e_shnum;
    strndx=elfe->e_shstrndx;

    /* on prend tout les headers */
    buffer=realloc(buffer, sizeof(Elf64_Shdr)*snum);
    fseek(fpin, soff, SEEK_SET);
    fread(buffer, sizeof(Elf64_Shdr)*snum, 1, fpin);
    /* on ne prend que le shstrtab dans un premier temps */
    elfs=(Elf64_Shdr*)(buffer+strndx*sizeof(Elf64_Shdr));
    stroff=elfs->sh_offset;

    /* on prend la section shstrtab */
    bufname=malloc(elfs->sh_size);
    fseek(fpin, stroff, SEEK_SET);
    fread(bufname, elfs->sh_size, 1, fpin);

    /* on parcours les sections a la recherche de la bonne */
    ptr=buffer;
    i=0;
    do {
        ptr+=sizeof(Elf64_Shdr);
        elfs=(Elf64_Shdr*)ptr;
        ++i;
    } while(strcmp(av[2], bufname+elfs->sh_name) && i<snum);
    if (i==snum) {
        fprintf(stdout, "section non trouve! :(\n");
        ecode=1;
        goto fin; // fu** c'est pratique les goto
    }
    fprintf(stdout, "section trouve, c'est glop! :)\n");
    fprintf(stdout, "offset: 0x%08X\tsize: 0x%08X\n", elfs->sh_offset, elfs->sh_size);

    /* c'est cool on l'a trouve, on cree le fichier */
    bufname=realloc(bufname, elfs->sh_size);
    fpout=fopen(av[3], "a+");
    fseek(fpin, elfs->sh_offset, SEEK_SET);
    i=fread(bufname, elfs->sh_size, 1, fpin);
    fwrite(bufname, elfs->sh_size, 1, fpout);

    /* et hop on part */
    fclose(fpout);
fin:
    free(bufname);
    free(buffer);
    fclose(fpin);
    return ecode;
}
