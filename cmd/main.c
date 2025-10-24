#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>

// структура для представления 512 битного числа
typedef struct {
    uint64_t parts[8];
} int512;

// структура, в которой передаем данные для функции потока
typedef struct {
    int start;
    int end;
    char **numbers;
    int512 *part_sum;
    int *part_count;
    pthread_mutex_t *mutex;
}trans_data;

void hex_to_int512(const char *hex, int512 *result) {
    memset(result, 0, sizeof(int512)); 
    
    int len = strlen(hex);
    int part_idx = 7; 
    int shift = 0;

    for (int i = len - 1; i >= 0; i--) {
        uint64_t digit;
        if (hex[i] >= '0' && hex[i] <= '9') {
            digit = hex[i] - '0';
        } else if (hex[i] >= 'a' && hex[i] <= 'f') {
            digit = hex[i] - 'a' + 10;
        } else if (hex[i] >= 'A' && hex[i] <= 'F') {
            digit = hex[i] - 'A' + 10;
        } else {
            continue;
        }
        

        result->parts[part_idx] |= (digit << (shift * 4));
        shift++;
        
        if (shift == 16){ 
            shift = 0;
            part_idx--;
            if (part_idx < 0) break;
        }
    }
}

void int512_add(const int512 *a, const int512 *b, int512 *result) {
    uint64_t ost = 0;
    
    for (int i = 7; i >= 0; i--) {
        uint64_t sum = a->parts[i] + b->parts[i] + ost;
        result->parts[i] = sum;
        ost = (sum < a->parts[i]) ? 1 : 0;
    }
}

void int512_div(const int512 *a, uint64_t divisor, int512 *result) {
    memset(result, 0, sizeof(int512));
    
    if (divisor == 0) {
        fprintf(stderr, "division = 0\n");
        return;
    }
    
    uint64_t ost = 0;
    
    for (int part_idx = 0; part_idx < 8; part_idx++) {
        uint64_t current_part = a->parts[part_idx];
        
        for (int bit_idx = 63; bit_idx >= 0; bit_idx--) {
            ost = (ost << 1) | ((current_part >> bit_idx) & 1);
            
            if (ost >= divisor) {
                ost -= divisor;
                result->parts[part_idx] |= (1ULL << bit_idx);
            }
        }
    }

    if ((ost << 1) >= divisor) {
        uint64_t carry = 1;
        for (int i = 7; i >= 0; i--) {
            result->parts[i] = result->parts[i] + carry;
            
            if (result->parts[i] < carry){
                carry = 1;
            } else{
                break;
            }
        }
    }
}

void int512_to_hex(const int512 *num, char *result) {
    char temp[129] = {0};
    int pos = 0;
    
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 16; j++) {
            int digit = (num->parts[i] >> (60 - j*4)) & 0xF;
            temp[pos+j] = "0123456789ABCDEF"[digit];
        }
        pos += 16;
    }

    int start = 0;
    while (temp[start] == '0' && start < 127) {
        start++;
    }
    
    for (int i = start;  i < 128; i++){
        result[i-start] = temp[i];
    }

    result[128-start] = '\0';
}

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