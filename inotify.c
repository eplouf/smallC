#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <errno.h>
#include <sys/select.h>

/* 
  séquentiel, que des files handle bloquants, stable au maximum pour un outil
  de base!
  donc non, ca a très vite tourner en polling...
  donc, à faire plutot une boucle ultra simple, qui poll le fd, si c'est ok, alors ca envoi ALRM

  le but: prendre un fichier dès qu'il apparait dans le rép /X,
  le supprimer/déplacer vers un autre endroit, en gardant les informations locales:
    - temps
    - nom d'hote
  utiliser soit rsync(code simple donc possiblement - de failles) ou libssh2 de aris(fame lol)
*/

/* tout ceci n'est à utiliser que à partir de 2.6.25~ */
/*struct inotify_event {
  int wd; // Watch descriptor /
  uint32_t mask; / mask of events
    interest in IN_CREATE and moreover IN_CLOSE_WRITE, for consistency ;)
    intersting false case are IN_DONT_FOLLOW(symlinks), IN_MASK_ADD(modif on mask..?), 
    IN_IGNORED(someone or the code removed the hook by inotify_rm_wathc() or file supressed),
    IN_Q_OVERFLOW(real problem under kernel, too much!?!)
  /
  uint32_t cookie; // cookie for related events, see rename (2)
  uint32_t len; // size of name field, pascal way
  char name[]; // the file which is created, and write+closed
};*/
/* read of size (sizeof(struct inotify_event) + NAME_MAX + 1
   1 étant le \0 ..
    par contre, ioctl(FIONREAD) permet de voir le goatse^W le nombre de bits qu'il faut lire
  et donc oui c'est un problème si un read ne se recoit pas tout, d'ailleurs, 
  j'affiche la taille recu de read ?
*/

/* interesting proc,
  /proc/sys/fs/inotify/max_queued_events
  /proc/sys/fs/inotify/max_user_instances
  /proc/sys/fs/inotify/max_user_watches
*/

/* bugs: on peut recevoir plusieurs event pour le même fichier!?! apparement ca se produit pas
  donc poser les derniers fichiers vu dans une liste LIFO, see inotify(7)#BUGS
*/

/* to make this one stable:
 *  - the alarm scheme could make it possible to have a race over the signal handler,
 *    i doubt he should react ok, as each treatment is not related to others... but heh?
 *    a semaphore could ease the thing to work better, OR there is the evolved setitimer which can
 *    be used for verifying if the alarm will be executed in few times, or not
 *  - i catch for create file and closeonwrite, the least is coherent, but the create file
 *    could help to truncated files..i was talking about coredump, and i should trust the kernel..ahah
 *  - goes for a method to upload the file
 */

/* TODO: 
 *  - syslog
 *  - choice at runtime of destination(but should have keys included! so it will fail, test :)
 *  - just, i do not want to ps, see the task, and make the assomption that all is going,
 *    whereas, if this tool is public, a malicious could as replaced it... so:
 *      - i need ninja?
 *      - do not want to ease the change of remote, like the hardcode + external tool to hardened it
 *  - could track multiple paths
 *    - and do different actions among (link to regexp)
*/

#define DELAY 0
#define DELAYS 1
#define NAMEOFF (sizeof(int)+sizeof(unsigned int)*3)
#define IEVTSZ (sizeof(struct inotify_event)+NAME_MAX+1)
#define SZMIN (sizeof(int)+sizeof(uint32_t)*3)

int inode, iwatch;
char *pathname=NULL;

int fileInfo(char *path, char *file) {
  char *pathfile=NULL;
  struct stat sb;
  int isstat=0, sz;

  sz = strlen(path) + strlen(file)*2 + sizeof(char)*2 + sizeof("mv /home/vrac/ "); 
  pathfile = malloc(sz);
  snprintf(pathfile, sz, "%s/%s", path, file);
  fprintf(stdout, "received %d, file is %s\n", sz, pathfile);
  isstat = stat(pathfile, &sb);
  snprintf(pathfile, sz, "mv %s/%s /home/vrac/%s", path, file, file);
  system(pathfile);
  free(pathfile);
  if (!isstat) {
    switch (sb.st_mode & S_IFMT) {
    case S_IFBLK:  printf("block device\n");            break;
    case S_IFCHR:  printf("character device\n");        break;
    case S_IFDIR:  printf("directory\n");               break;
    case S_IFIFO:  printf("FIFO/pipe\n");               break;
    case S_IFLNK:  printf("symlink\n");                 break;
    case S_IFREG:  printf("regular file\n");            break;
    case S_IFSOCK: printf("socket\n");                  break;
    default:       printf("unknown?\n");                break;
    }
    printf("I-node number: %ld\n", (long) sb.st_ino);
    printf("Mode:          %lX\n", (unsigned long) sb.st_mode);
    printf("Link count:    %ld\n", (long) sb.st_nlink);
    printf("Ownership:    UID=%ld   GID=%ld\n", (long) sb.st_uid, (long) sb.st_gid);
    printf("Preferred I/O block size: %ld bytes\n", (long) sb.st_blksize);
    printf("File size:                %lld bytes\n", (long long) sb.st_size);
    printf("Blocks allocated:         %lld\n", (long long) sb.st_blocks);
    printf("Last status change:       %s", ctime(&sb.st_ctime));
    printf("Last file access:         %s", ctime(&sb.st_atime));
    printf("Last file modification:   %s", ctime(&sb.st_mtime));
  }
  return 0;
}

// if there is more inotify event to read
int inQueue(int fd) {
  int fds;
  fd_set read;
  struct timeval tv={DELAYS, DELAY};

  FD_ZERO(&read);
  FD_SET(fd, &read);
  fds=fd+1;
  select(fds, &read, NULL, NULL, &tv);
  if (FD_ISSET(fd, &read)) {
    return 1;
  }
  return 0;
}

// read inotify event, extract filename, and call action
int catchFile() {
  int sz=0;
  struct inotify_event *evt=NULL;
  char *buffer, *filename;

  buffer = malloc(IEVTSZ);
  do {
	  buffer = realloc(buffer, IEVTSZ);
	  sz = read(inode, buffer, IEVTSZ);
    // devrait plutôt être sur > szmin(= evt->len), que sz > 0
    if (sz < 0) {
      continue;
    } else if (sz > SZMIN) {
	    evt = (struct inotify_event*) buffer;
	    if (evt->mask & (IN_CLOSE_WRITE)) {
	      filename = buffer + NAMEOFF;
	      *(filename + evt->len) = '\0';
	      fprintf(stdout, "nouveau fichier(ou partie)! %do (min:%ld) %s / %s\n", sz, SZMIN, pathname, filename);
        fileInfo(pathname, filename);
        /*evt = next + sizeof(struct inotify_event);
        next = evt;*/
        /* buffer += sizeof(struct inotify_event);
          */
      }
	  }
  } while (inQueue(inode));
  free(buffer);
  return 0;
}

// trap sigquit
int sigQuit(int signum) {
  free(pathname);
  inotify_rm_watch(inode, iwatch);
  close(inode);
  exit(signum);
  return 1;
}

int main(int ac, char **av) {

  // TODO usage()

  pathname = realpath(av[1], NULL);
  if (pathname == NULL) {
    return -1;
  }
  fprintf(stdout, "path: %s\n", pathname);

  // trap, read() error EINTR on ctrl+c
  signal(SIGINT, (__sighandler_t)sigQuit);

  inode=inotify_init1(IN_NONBLOCK);
  iwatch = inotify_add_watch(inode, pathname, IN_CLOSE_WRITE);
  if (iwatch < 0) {
    perror("inotify add:");
    return -3;
  }

  fprintf(stdout, "listening...\n");
  while (1) {
    if (inQueue(inode)) {
      catchFile();
    }
  }

  return 1;
}
