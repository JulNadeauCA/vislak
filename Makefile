TOP=	.
include Makefile.config

PROJECT=	"vislak"

PROG=		vislak
PROG_TYPE=	"GUI"
PROG_GUID=	"5594933c-0b4b-4fcd-a68e-f215666d3194"

SRCS=	vislak.c \
	vs_clip.c \
	vs_view.c \
	vs_midi.c \
	vs_player.c \
	vs_project.c \
	vs_gui.c

#SHARE=	vislak.png

CFLAGS+=${AGAR_CFLAGS} ${AGAR_MATH_CFLAGS} ${GETTEXT_CFLAGS} ${JPEG_CFLAGS} \
	${ALSA_CFLAGS} ${SNDFILE_CFLAGS}
LIBS=	${PORTAUDIO_LIBS} ${AGAR_LIBS} ${AGAR_MATH_LIBS} ${GETTEXT_CFLAGS} ${JPEG_LIBS} \
	${ALSA_LIBS} ${SNDFILE_LIBS}

all: all-subdir ${PROG}

configure: configure.in
	cat configure.in | mkconfigure > configure
	chmod 755 configure

.PHONY: configure

include ${TOP}/mk/build.prog.mk
