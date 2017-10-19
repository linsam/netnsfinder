#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>

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
    const char *path;
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
            list->path = path;
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
                list->path = path;
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
                list->path = path;
            }
            return;
        }
        list = list->next;
    }
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
    /* TODO: Also look for nsfs mounts */
    struct nslist *list = netHead;
    struct nslist *last = NULL;
    while (list) {
        if (list->pid && list->path) {
            printf("%lx (%li) via %i %s\n", list->inode, list->inode, list->pid, list->path);
        } else if (list->pid) {
            printf("%lx (%li) via %i\n", list->inode, list->inode, list->pid);
        } else if (list->path) {
            printf("%lx (%li) via %s\n", list->inode, list->inode, list->path);
        } else {
            /* shouldn't reach here */
            printf("%lx (%li) via <unknown>\n", list->inode, list->inode);
        }
        //last = list;
        list = list->next;
        //free(last);
    }

    closedir(dir);
    return 0;
}

