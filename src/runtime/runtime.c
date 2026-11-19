// RV32IM / ILP32 C ABI shared by the IR and assembly paths.
typedef __INT32_TYPE__ int32_t;
typedef __UINT32_TYPE__ uint32_t;

extern int scanf(const char *format, ...);
extern int printf(const char *format, ...);
extern void *malloc(__SIZE_TYPE__ size);

int32_t get_i32(void){
    int32_t value = 0;
    (void)scanf("%d", &value);
    return value;
}

void print_i32(int32_t value){
    (void)printf("%d", value);
}

void println_i32(int32_t value){
    (void)printf("%d\n", value);
}

void *__rx_alloc(uint32_t size, uint32_t alignment){
    // REIMU malloc satisfies all Rx storage alignments (1 or 4 bytes).
    (void)alignment;
    return malloc(size);
}
