lib.name = xeq

class.sources = src/xeq.c \
                src/xeq_host.c \
                src/xeq_parse.c \
                src/xeq_polyparse.c \
                src/xeq_record.c \
                src/xeq_follow.c \
                src/xeq_data.c \
                src/xeq_polytempo.c \
                src/xeq_time.c \
                src/xeq_query.c

shared.sources = shared/sq.c \
                 shared/dict.c \
                 shared/bifi.c \
                 shared/mifi.c \
                 shared/mfbb.c \
                 shared/hyphen.c \
                 shared/text.c \
                 shared/vefl.c

cflags = -I./shared

datafiles = LICENSE.txt

datadirs = doc/examples

suppress-wunused = yes

include Makefile.pdlibbuilder
