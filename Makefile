TARGET = not_in.so
CFLAGS = -O2 -fPIC -I/usr/include/mysql -DHAVE_DLOPEN=1

# Where to copy the final library
DEST	= /usr/lib/mysql/plugin

SRCS = 	not_in.c

OBJS =  $(SRCS:%.c=%.o)

all: $(TARGET)

install: $(TARGET)
	cp $(TARGET) $(DEST)

clean:
	$(RM) $(OBJS) $(TARGET)

%.o: %.c
	$(CXX) -o $@ $(CFLAGS) -c $<

$(TARGET): $(OBJS)
	$(LD) -shared -o $(TARGET) $(OBJS)