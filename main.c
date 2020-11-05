#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <limits.h>

#define A 90
#define D 109
#define E 164
#define G 13
#define I 53
FILE *URANDOM;
int stop = 0;
pthread_mutex_t mutex;
pthread_cond_t cv;
int started_to_fill = 0;
typedef struct {
    int number;
    int size_of_mem;
    int *start;
} generator;

typedef struct {
    int *start;
    int size;
} writer;

typedef struct {
    int number;
} reader;

char* seq_read(int fd, int fsize) {
    char *buffer = (char *) malloc(fsize);
    int blocks = fsize / G;
    int last_block_size = fsize % G;
    for (int i = 0; i < blocks; i++)
        pread(fd,buffer + G * i , G, G * i);
    if (last_block_size > 0)
        pread(fd, buffer + G * blocks, last_block_size, G * blocks);
    return buffer;
}

void seq_write(void *ptr, int size, int n, int fd, const char* filepath) {
    struct stat fstat;
    stat(filepath, &fstat);
    int blksize = (int) fstat.st_blksize;
    int align = blksize - 1;
    int bytes = size * n;
    int blocks = bytes / blksize;

    char *buff = (char *) malloc((int) blksize + align);
    char *wbuff = (char *) (((uintptr_t) buff + align) & ~((uintptr_t) align));

    for (int i = 0; i < blocks; i++) {
        char *buf_ptr = ptr + blksize * i;
        for (int j = 0; j < blksize; j++)
            buff[j] = buf_ptr[j];

        if (pwrite(fd, wbuff, blksize, blksize * i) < 0) {
            free((char *) buff);
            printf("ошибка при записи\n");
            return;
        }
    }
    free((char *) buff);
}

int read_int_from_file(FILE *file) {
    int i = 0;
    fread(&i, 4, 1, file);
    return i;
}
void *fill_area(void *thread_data) {
    generator *gen = (generator *) thread_data;
    atomic_fetch_add(&started_to_fill, 1);
    if (started_to_fill == D)
        printf("Область заполнена данными\n");
    do {
        for (int i = 0; i < gen->size_of_mem; i++)
            gen->start[i] = read_int_from_file(URANDOM);
    } while (!stop);
    return NULL;
}
void *to_read(void *thread_data) {
    reader *data = (reader *) thread_data;
    printf("READ %d запущен\n", data->number);
    do {
        char *fname = "labfile";
        int file_desc = -1;
        while (file_desc == -1) {
            printf("Reader %d в ожидании\n", data->number);
            pthread_mutex_lock (&mutex);
            pthread_cond_wait(&cv, &mutex);
            printf("Reader %d захватил мьютекс\n", data->number);
            file_desc = open(fname, O_RDONLY, 00666);
            if (file_desc == -1) {
                pthread_mutex_unlock (&mutex);
                printf("Reader %d освободил мьютекс\n", data->number);
                printf("Reader %d ошибка при открытии файла\n", data->number);
            }
        }
        struct stat st;
        stat(fname, &st);
        int file_size = st.st_size;
        char *buffer = seq_read(file_desc, file_size);
        close(file_desc);
        int *int_buf = (int *) buffer;
        int max = INT_MIN;
        for (int i = 0; i < file_size / 4; i++)
            max = int_buf[i]>max?int_buf[i]:max;
        printf("READ %d Максимум: %d.\n", data->number, max);
        free(buffer);
        pthread_mutex_unlock (&mutex);
        printf("READ %d освободил мьютекс\n", data->number);
    } while (!stop);
    return NULL;
}
void *to_write(void *thread_data) {
    writer *data = (writer *) thread_data;
    printf("WRITE запущен\n");
    do {
        char fname[7] = "labfile";
        printf("WRITE в ожидании\n");
        pthread_mutex_lock (&mutex);
        printf("WRITE захватил мьютекс \n");
        int file = open(fname, O_WRONLY | O_CREAT | __O_DIRECT, 00666);
        printf("WRITE начал запись\n");
        //Так как E>A, в файл помещается вся область
        seq_write(data->start, sizeof(int), data->size, file, fname);
        close(file);
        printf("WRITE закончил запись\n");
        pthread_cond_broadcast(&cv);
        pthread_mutex_unlock (&mutex);
        printf("WRITE освободил мьютекс \n");
    } while (!stop);
    return NULL;
}

int main() {
    URANDOM = fopen("/dev/urandom", "r");

    printf("До аллокации");
    getchar();
    int *area_of_mem = malloc(A * 1024 * 1024);

    pthread_t *generator_threads = (pthread_t *) malloc(D * sizeof(pthread_t));
    generator *generator_data = (generator *) malloc(D * sizeof(generator));
    printf("После аллокации");
    getchar();

    int int_mem = A * 1024 * 1024 / 4;
    int size_of_mem = int_mem / D;
    int *start = area_of_mem;
    for (int i = 0; i < D; i++) {
        generator_data[i].number = i;
        generator_data[i].size_of_mem = size_of_mem;
        generator_data[i].start = start;
        start += size_of_mem;
    }
    generator_data[D - 1].size_of_mem += int_mem % D;

    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cv, NULL);

    pthread_t *thread_writer = (pthread_t *) malloc(sizeof(pthread_t));
    writer *writer_data = (writer *) malloc(sizeof(writer));
    writer_data->start = area_of_mem;
    writer_data->size = int_mem;

    pthread_t *reader_threads = (pthread_t *) malloc(I * sizeof(pthread_t));
    reader *reader_data = (reader *) malloc(I * sizeof(reader));
    for (int i = 0; i < I; i++)
        reader_data[i].number = i;

    printf("Генерация данных запущена\n");
    for (int i = 0; i < D; i++)
        pthread_create(&(generator_threads[i]), NULL, fill_area, &generator_data[i]);
    printf("Запись запущена\n");
    pthread_create(thread_writer, NULL, to_write, writer_data);
    printf("Чтение запущено\n");
    for (int i = 0; i < I; i++)
        pthread_create(&(reader_threads[i]), NULL, to_read, &reader_data[i]);

    getchar();
    stop = 1;
    for (int i = 0; i < I; i++)
        pthread_join(reader_threads[i], NULL);
    printf("Чтение прекращено\n");
    pthread_join(*thread_writer, NULL);
    printf("Запись прекращена\n");
    for (int i = 0; i < D; i++)
        pthread_join(generator_threads[i], NULL);
    printf("Генерация данных прекращена\n");

    fclose(URANDOM);
    free(generator_threads);
    free(generator_data);
    free(thread_writer);
    free(writer_data);
    free(reader_threads);
    free(reader_data);
    free(area_of_mem);
    printf("Деаллокация совершена");
    getchar();
    return 0;
}