CFLAGS += -Iglad/include -Wall -Wextra -pedantic -Werror

OBJS = src/main.o \
       glad/src/gl.o

ifndef NFRACTAL_PLATFORM
	NFRACTAL_PLATFORM = x11
endif

ifeq (${NFRACTAL_PLATFORM},x11)
	GRAPHICS_LIBS = -lGL
else ifeq (${NFRACTAL_PLATFORM},wayland)
	GRAPHICS_LIBS = -lwayland-client -lwayland-cursor -lwayland-egl
else
	$(error "Graphics platform should be either X11 or Wayland!")
endif

nfractal : ${OBJS}
	${CC} ${CFLAGS} -o nfractal ${OBJS} -lm -lglfw ${GRAPHICS_LIBS}

.PHONY : clean
clean :
	rm ${OBJS}
