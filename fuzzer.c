#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>

#define MAX_THREADS 128

typedef struct {
    char* path;
    bool can_read;
    bool can_write;
} FileInfo;

typedef struct {
    FileInfo* listing;
    size_t size;
} Listing;

void listdirs(const char* dir_path, Listing* output_list);
void* worker(void* arg);
void daemonize();
size_t get_random(size_t max);


void listdirs(const char* dir_path, Listing* output_list) {
    DIR* dir = opendir(dir_path);
    if (!dir) {
        fprintf(stderr, "Failed to open directory: %s\n", dir_path);
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char path[PATH_MAX];
        snprintf(path, PATH_MAX, "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (lstat(path, &st) != 0) {
            fprintf(stderr, "Failed to get file metadata: %s\n", path);
            continue;
        }

        if (S_ISLNK(st.st_mode)) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            listdirs(path, output_list);
        } else if (S_ISREG(st.st_mode)) {
            FileInfo file_info;
            file_info.path = strdup(path);
            file_info.can_read = access(path, R_OK) == 0;
            file_info.can_write = access(path, W_OK) == 0;

            output_list->size++;
            output_list->listing = (FileInfo*)realloc(output_list->listing, output_list->size * sizeof(FileInfo));
            output_list->listing[output_list->size - 1] = file_info;
        }
    }

    closedir(dir);
}

void* worker(void* arg) {
    Listing* listing = (Listing*)arg;

    // Fuzz buffer
    unsigned char buf[8192];
    memset(buf, 0x41, sizeof(buf));

    // Fuzz forever
    while (1) {
        size_t rand_file = get_random(listing->size);
        FileInfo file_info = listing->listing[rand_file];

        if (strncmp(file_info.path, "/proc/", 6) == 0 && isdigit(file_info.path[6])) {
            continue;
        }

        if (file_info.can_read) {
            // Fuzz by reading
            int fd = open(file_info.path, O_RDONLY);
            if (fd != -1) {
                size_t fuzz_size = get_random(sizeof(buf));
                read(fd, buf, fuzz_size);
                close(fd);
            }
        }

        if (file_info.can_write) {
            // Fuzz by writing
            int fd = open(file_info.path, O_WRONLY);
            if (fd != -1) {
                size_t fuzz_size = get_random(sizeof(buf));
                write(fd, buf, fuzz_size);
                close(fd);
            }
        }
    }

    return NULL;
}

void daemonize() {
    printf("Daemonizing\n");

    if (daemon(0, 0) != 0) {
        fprintf(stderr, "Failed to daemonize\n");
        exit(1);
    }

    // Sleep to allow a physical cable swap
    sleep(10);
}

size_t get_random(size_t max) {
    return rand() % max;
}

int main() {
    printf("Starting...\n");

    Listing dirlisting;
    dirlisting.size = 0;
    listdirs("/", &dirlisting);

    printf("Created listing of %zu files\n", dirlisting.size);

    if (dirlisting.size == 0) {
        fprintf(stderr, "Directory listing was empty\n");
        return 1;
    }

    // Spawn fuzz threads
    pthread_t threads[MAX_THREADS];
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_create(&threads[i], NULL, worker, &dirlisting);
    }

    // Wait for all threads to complete
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("All threads completed");

    return 0;
}