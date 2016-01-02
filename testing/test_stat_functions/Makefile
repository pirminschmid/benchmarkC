CFLAGS  = -Wall -Wextra -std=c99 -O3 -march=native

TARGET = test_stat_functions_main
SRCS   = test_stat_functions.c benchmark.c
OBJS   = $(SRCS:.c=.o)
ASM    = $(SRCS:.c=.S)  
DEPS   = $(SRCS:%.c=.%.d)

.PHONY: clean all
all: $(TARGET) $(ASM)

run: $(TARGET)
	./$(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) -lm

$(ASM): $(SRCS)
	$(CC) -MMD -MP -MF .$*.d $(CFLAGS) -c $*.c -S -o $*.S

%.o: %.c
	$(CC) -MMD -MP -MF .$*.d $(CFLAGS) -c $*.c -o $*.o

clean:
	$(RM) $(TARGET) $(OBJS) $(DEPS) $(ASM)

-include $(DEPS)
