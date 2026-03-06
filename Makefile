CC = gcc

OBJDIR = build
TARGET = $(OBJDIR)/out
SRCS = main.c parse_config.c
OBJS = $(addprefix $(OBJDIR)/, $(SRCS:.c=.o))
CFLAGS=$(shell pkg-config --cflags libevdev) -std=gnu11
LDFLAGS=$(shell pkg-config --libs libevdev) -lm

all: debug

debug: CFLAGS += -Wextra -Wall -g -O0
debug: $(OBJDIR) $(TARGET)

release: CFLAGS += -O3
release: $(OBJDIR) $(TARGET)
	strip $(TARGET)
	rm -v $(OBJDIR)/*.o

clean:
	rm -frv $(OBJDIR)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

$(OBJDIR)/%.o: %.c
	@mkdir -pv $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -pv $(OBJDIR)
