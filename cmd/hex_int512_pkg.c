#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct {
    uint64_t parts[8];
} int512;


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