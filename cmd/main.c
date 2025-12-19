#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include "hex_int512_pkg.c"

// структура, в которой передаем данные для функции потока
typedef struct {
    int start;
    int end;
    char **numbers;
    int512 *part_sum;
    int *part_count;
    pthread_mutex_t *mutex;
}trans_data;


// процесс выполняемый потоком
void* process(void *arg) {
    trans_data *data = (trans_data *)arg;
    int512 local_sum = {0};
    int local_count = 0;
    
    for (int i = data->start; i < data->end; i++) {
        if (data->numbers[i] != NULL) {
            int512 current_num;
            hex_to_int512(data->numbers[i], &current_num);
            
            int512 new_sum;
            int512_add(&local_sum, &current_num, &new_sum);
            local_sum = new_sum;
            local_count++;
        }
    }

    pthread_mutex_lock(data->mutex);
    
    int512 new_total_sum;
    int512_add(data->part_sum, &local_sum, &new_total_sum);
    *(data->part_sum) = new_total_sum;
    *(data->part_count) += local_count;
    
    pthread_mutex_unlock(data->mutex);
    
    return NULL;
}

double get_current_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("usage: %s <input_file> <max_threads> <max_memory_mb>\n", argv[0]);
        return 1;
    }
    
    const char *filename = argv[1];
    int max_threads = atoi(argv[2]);
    int max_memory_mb = atoi(argv[3]);
    
    if (max_threads <= 0) {
        fprintf(stderr, "max_threads must be > 0 = 0\n");
        return 1;
    }
    
    if (max_memory_mb <= 0) {
        fprintf(stderr, "max_memory_mb must be > 0 = 0\n");
        return 1;
    }
    
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("failed to open file");
        return 1;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    //printf("File size: %ld bytes\n", file_size);
    
    size_t max_memory_bytes = max_memory_mb * 1024 * 1024;
    if ((size_t)file_size > max_memory_bytes) {
        fprintf(stderr, "file size (%ld bytes) > (%zu bytes)\n", 
               file_size, max_memory_bytes);
        fclose(file);
        return 1;
    }
    
    char *file_buffer = malloc(file_size + 1);
    if (!file_buffer) {
        perror("failed to allocate memory for file buffer");
        fclose(file);
        return 1;
    }
    
    size_t bytes_read = fread(file_buffer, 1, file_size, file);
    file_buffer[bytes_read] = '\0';
    fclose(file);
    
    int capacity = 1000;
    int count_lines = 0;
    char **numbers = malloc(capacity * sizeof(char*));
    if (!numbers) {
        perror("failed to allocate memory for numbers array");
        free(file_buffer);
        return 1;
    }
    
    char *line = strtok(file_buffer, "\n");
    while (line != NULL) {
        if (count_lines >= capacity) {
            capacity *= 2;
            char **new_numbers = realloc(numbers, capacity * sizeof(char*));
            if (!new_numbers) {
                perror("Failed to reallocate memory for numbers array");
                free(numbers);
                free(file_buffer);
                return 1;
            }
            numbers = new_numbers;
        }
        numbers[count_lines++] = line;
        line = strtok(NULL, "\n");
    }

    int num_threads = (count_lines < max_threads) ? count_lines : max_threads;
    int numbers_per_thread = count_lines / num_threads;
    int remainder = count_lines % num_threads;
    
    printf("using %d threads\n", num_threads);
    printf("numbers per thread: %d (with %d remainder)\n", numbers_per_thread, remainder);
    
    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    trans_data *thread_data = malloc(num_threads * sizeof(trans_data));
    pthread_mutex_t mutex;
    
    if (!threads || !thread_data) {
        perror("failed to allocate memory for threads");
        free(file_buffer);
        free(numbers);
        free(threads);
        free(thread_data);
        return 1;
    }
    
    if (pthread_mutex_init(&mutex, NULL) != 0) {
        perror("failed to initialize mutex");
        free(file_buffer);
        free(numbers);
        free(threads);
        free(thread_data);
        return 1;
    }
    
    int512 total_sum = {0};
    int total_count = 0;
    
    double start_time = get_current_time();
    
    int current_start = 0;
    for (int i = 0; i < num_threads; i++) {
        thread_data[i].numbers = numbers;
        thread_data[i].start = current_start;
        thread_data[i].end = current_start + numbers_per_thread + (i < remainder ? 1 : 0);
        thread_data[i].part_sum = &total_sum;
        thread_data[i].part_count = &total_count;
        thread_data[i].mutex = &mutex;
        
        current_start = thread_data[i].end;
        
        if (pthread_create(&threads[i], NULL, process, &thread_data[i]) != 0) {
            perror("Failed to create thread");
            free(file_buffer);
            free(numbers);
            free(threads);
            free(thread_data);
            pthread_mutex_destroy(&mutex);
            return 1;
        }
    }
    
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double end_time = get_current_time();
    double execution_time = end_time - start_time;
    
    int512 average = {0};
    if (total_count > 0) {
        int512_div(&total_sum, total_count, &average);
    }
    
    char avg_hex[129];
    int512_to_hex(&average, avg_hex);
    
    printf("\n-------results-------:\n");
    printf("    total count of numbers: %d\n", total_count);
    printf("    execution time: %.6f seconds\n", execution_time);
    printf("    average hex: \n%s\t\n\n", avg_hex);
    double performance = total_count / execution_time;
    printf("performance: %.2f numbers/second\n", performance);
    
    size_t memory_used = file_size + (capacity * sizeof(char*)) + (num_threads * sizeof(pthread_t)) + (num_threads * sizeof(trans_data));
    printf("memory used: ~%.2f mb\n", memory_used / (1024.0 * 1024.0));
    
    pthread_mutex_destroy(&mutex);
    free(file_buffer);
    free(numbers);
    free(threads);
    free(thread_data);

    printf("\ncheck thread count:\n");
    printf("  ps -eLf | grep %s | grep -v grep\n\n", argv[0]);
    printf("  htop -p %d\n\n", getpid());
    printf("  cat /proc/%d/status | grep Thread\n", getpid());
    
    return 0;
}

/*
    запуск через терминал
    gcc -o res cmd/main.c
    ./res numbers.txt 4 1024
*/ 