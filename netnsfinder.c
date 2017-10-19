#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sched.h>
#include <errno.h>

/** Check if a string consists solely of an integer number.
 *
 * @retval 1 is a number only
 * @retval 0 some non-number character is present
 */
int
isint(const char *s) {
    while (*s) {
        if (!isdigit(*s++)) {
            return 0;
        }
    }
    return 1;
}

/* each list element has an inode number, and either a PID or mount path
 * that is holding the inode open. In some instances, we might have both
 * PID and path. */
struct nslist {
    ino_t inode;
    pid_t pid;
    char *path;
    struct nslist *next;
};

struct nslist *netHead = NULL;

/* Add an entry to the list if it doesn't already exist.
 *
 * If it does already exist (identified by inode), will fill in the pid or
 * path if given and not already present.
 */
void
nslistAddUnique(struct nslist **head, ino_t id, pid_t pid, const char *path)
{
    struct nslist *list;
    if (!*head) {
        /* special case, create new list */
        list = malloc(sizeof(*list));
        if (list) {
            list->inode = id;
            list->next = NULL;
            list->pid = pid;
            list->path = path ? strdup(path) : NULL;
            *head = list;
        }
        return;
    }
    list = *head;

    while (list) {
        if (list->inode == id) {
            /* already have it; check for new info */
            if (!list->pid) {
                list->pid = pid;
            }
            if (!list->path) {
                list->path = path ? strdup(path) : NULL;
            }
            return;
        }
        if (list->next == NULL) {
            /* didn't find it; add this one */
            list->next = malloc(sizeof(*list));
            list = list->next;
            if (list) {
                list->inode = id;
                list->next = NULL;
                list->pid = pid;
                list->path = path ? strdup(path) : NULL;
            }
            return;
        }
        list = list->next;
    }
}

/** Allocate a new string containing a line from the opened file.
 *
 * Caller is responsible for freeing the returned string.
 *
 * quick-and-dirty implementation. Hopes that fread does its own buffering
 * of the input.
 */
char *
readline(FILE *file)
{
    /* TODO: We could dynamically grow the size of the line, possibly with
     * a limit to make sure we don't exceed reasonable memory usage.
     */
#define MAXLINE 512
    char *ret = malloc(MAXLINE);
    int i;
    for (i = 0; i < MAXLINE; i++) {
        size_t fret = fread(&ret[i], 1, 1, file);
        if (fret != 1) {
            /* EOF or error. Incomplete line either way */
            free(ret);
            return NULL;
        }
        if (ret[i] == '\n') {
            ret[i] = '\0';
            return ret;
        }
    }
    /* MAX size exceeded; bail */
    free(ret);
    return NULL;
}

/** Check if a path is a valid network namespace.
 *
 * @retval 1 path references a network namespace
 * @retval 0 path doesn't reference a network namespace, or an error
 * occured trying to find out.
 * @note This function has the side affect of placing the program into the
 * network namespace it is testing.
 * @todo This function could cache its original network namespace and
 * restore it.
 */
static int
isNetNs(const char *mountpoint)
{
    int ret = 0;
    int fd = open(mountpoint, O_RDONLY);
    if (fd < 0) {
        perror("failed to open");
        errno = 0;
        return 0;
    }
    int nsret = setns(fd, CLONE_NEWNET);
    if (nsret == 0) {
        ret = 1;
    } else {
        if (errno != EINVAL) {
            /* INVAL can mean it isn't a network type
             * namespace, so no need to print warnings
             * about that.
             */
            fprintf(stderr, "Couldn't check netns %s: %s\n", mountpoint, strerror(errno));
        }
        errno = 0;
    }
    close(fd);
    return ret;
}

int main()
{
    /* TODO: Need to enumerate /proc/mounts for nsfs, and search also mount
     * namespaces py pid and then by proc/mounts recursively
     *
     * For now, we'll just list netns visible to the parent via direct
     * mount and pid.
     */
    int res;
    struct stat stats;
    res = stat("/proc/1/ns/net", &stats);
    if (res != 0) {
        perror("Couldn't read top level netns");
        return 1;
    }
    ino_t netnsinode = stats.st_ino;
    nslistAddUnique(&netHead, stats.st_ino, 1, NULL);

    //printf("%i %lx\n", res, stats.st_ino);

    DIR *dir = opendir("/proc");
    if (!dir) {
        perror("couldn't open proc");
        return 1;
    }

    /* Scan PIDs */
    struct dirent *entry;
    while (entry = readdir(dir)) {
        if (isint(entry->d_name)) {
            char path[256]; /* TODO: Could this overflow? */
            snprintf(path, 255, "/proc/%s/ns/net", entry->d_name);
            path[255] = '\0';
            res = stat(path, &stats);
            if (res == 0) {
                if (stats.st_ino != netnsinode) {
                    //printf(" %s -> %lx\n", entry->d_name, stats.st_ino);
                    nslistAddUnique(&netHead, stats.st_ino, atoi(entry->d_name), NULL);
                }
            }
        }
    }
    closedir(dir);

    /* Look for nsfs mounts */
    FILE *mounts = fopen("/proc/mounts", "r");
    if (!mounts) {
        perror("Couldn't open /proc/mounts");
        errno = 0;
    } else {
        char *line;
        char *s;
        while (line = readline(mounts)) {
            if (strncmp("nsfs ", line, 5) == 0) {
                /* /proc/mounts is space separated containing source, mount
                 * path, type, options, and 2 '0's so that it looks like
                 * the dump and fsck order values.
                 *
                 * But what if the source or mount point has spaces in it?
                 * Well, this file escapes spaces and possibly other
                 * values. E.g. the mount point "this is a test" is
                 * rendered as "this\040is\040a\040test". This means that
                 * after we find the mountpoint, we then have to un-escape
                 * the data. On the upside, we know that it will be
                 * smaller, so we can simply allocate the full size of the
                 * string.
                 */
                /* Could maybe use strsep instead? */
                char *mountpoint = line + 5;
                s = strchr(mountpoint, ' ');
                if (!s) {
                    fprintf(stderr, "Failure parsing /proc/mounts\n");
                    free(line);
                    break;
                }
                *s = '\0';
                res = stat(mountpoint, &stats);
                if (res == 0) {
                    if (isNetNs(mountpoint)) {
                        nslistAddUnique(&netHead, stats.st_ino, 0, mountpoint);
                    }
                }
            }
            free(line);
        }
        fclose(mounts);
    }



    /* Display results */
    struct nslist *list = netHead;
    struct nslist *last = NULL;
    while (list) {
        if (list->pid && list->path) {
            printf("%lx (%li) via %i or %s\n", list->inode, list->inode, list->pid, list->path);
        } else if (list->pid) {
            printf("%lx (%li) via %i\n", list->inode, list->inode, list->pid);
        } else if (list->path) {
            printf("%lx (%li) via %s\n", list->inode, list->inode, list->path);
        } else {
            /* shouldn't reach here */
            printf("%lx (%li) via <unknown>\n", list->inode, list->inode);
        }
        last = list;
        list = list->next;
        if (last->path) {
            free(last->path);
        }
        free(last);
    }
    return 0;
}

