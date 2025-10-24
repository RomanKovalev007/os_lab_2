#include <stdio.h>
#include <stdlib.h>

void generate_test_data(const char *filename, int num_numbers) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        perror("failed to create test file");
        exit(1);
    }
    
    for (int i = 0; i < num_numbers; i++) {
        char hex[129] = {0};
        
        for (int j = 0; j < 128; j++) {
            int digit = rand() % 16;
            hex[j] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        }
        
        fprintf(file, "%s\n", hex);
    }
    
    fclose(file);
    printf("test data generated in %s\n", filename);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <input_file>  <count_numbers>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    int count = atoi(argv[2]);
    
    generate_test_data(filename, count);
    return 0;
}


/*
    ./data numbers.txt 1024
*/