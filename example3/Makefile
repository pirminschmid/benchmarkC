CFLAGS  = -Wall -Wextra -std=c99 -O3 -march=native

TARGET = mmul
SRCS   = mmul.c benchmark.c
OBJS   = $(SRCS:.c=.o)
ASM    = $(SRCS:.c=.S)  
DEPS   = $(SRCS:%.c=.%.d)

.PHONY: clean all
all: $(TARGET) $(ASM) Makefile

run: $(TARGET)
	./$(TARGET)

$(TARGET): $(OBJS) Makefile
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) -lm

$(ASM): $(SRCS) Makefile
	$(CC) -MMD -MP -MF .$*.d $(CFLAGS) -c $*.c -S -o $*.S

%.o: %.c Makefile
	$(CC) -MMD -MP -MF .$*.d $(CFLAGS) -c $*.c -o $*.o

clean:
	$(RM) $(TARGET) $(OBJS) $(DEPS) $(ASM)

-include $(DEPS)
