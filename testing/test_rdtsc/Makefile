CFLAGS  = -Wall -Wextra -std=c99 -O0 -march=native -g

TARGET = test_rdtsc_main
SRCS   = test_rdtsc.c
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
