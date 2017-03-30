typedef struct clock_handler_t {
    struct timespec clocks[200];
    int counter;
}clock_handler;



void clock_init(clock_handler *c_k);
void clock_add(clock_handler *c_k);

void clock_display(clock_handler *c_k);